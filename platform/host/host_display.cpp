#include "host_display.h"

#include <new>

#include <embark/hal/types.h>
#include <embark_limits.h>
#include <middleware/elog/elog.hpp>

namespace embark::platform::host {
namespace {

constexpr std::uint8_t full_backlight = 100;

/// RGB565 逐通道按百分比缩放（背光模拟）。100% 由调用方直通，不走这里。
std::uint16_t scale_rgb565(std::uint16_t pixel, std::uint8_t percent) noexcept {
  const std::uint32_t red = (pixel >> 11U) & 0x1FU;
  const std::uint32_t green = (pixel >> 5U) & 0x3FU;
  const std::uint32_t blue = pixel & 0x1FU;
  const std::uint32_t scale = percent;
  return static_cast<std::uint16_t>(((red * scale / 100U) << 11U) | ((green * scale / 100U) << 5U) |
                                    (blue * scale / 100U));
}

}  // namespace

HostDisplay::HostDisplay(std::uint8_t window_scale, const char* title) noexcept
    : title_(title == nullptr ? "Embark 宿主" : title),
      window_scale_(window_scale == 0U ? 1U : window_scale) {}

HostDisplay::~HostDisplay() noexcept {
  destroy();
}

Error HostDisplay::init() noexcept {
  if (ready_) {
    return Error::none;  // 重复调用是允许的（HAL 契约：失败要能重试）
  }

  if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      ELOG_ERROR("显示：SDL_Init 失败（{}）", SDL_GetError());
      return Error::io_failure;
    }
    owns_video_ = true;
  }

  // 像素级放大要最近邻，否则 2 倍窗口会把界面糊成一团。
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

  const auto window_width = static_cast<int>(display_width) * static_cast<int>(window_scale_);
  const auto window_height = static_cast<int>(display_height) * static_cast<int>(window_scale_);
  window_ = SDL_CreateWindow(title_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width,
                             window_height, SDL_WINDOW_SHOWN);
  if (window_ == nullptr) {
    ELOG_ERROR("显示：SDL_CreateWindow 失败（{}）", SDL_GetError());
    destroy();
    return Error::io_failure;
  }

  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
  if (renderer_ == nullptr) {
    // 没有硬件加速就退回软件渲染：宿主仿真只需要"看得见、点得着"。
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  }
  if (renderer_ == nullptr) {
    ELOG_ERROR("显示：SDL_CreateRenderer 失败（{}）", SDL_GetError());
    destroy();
    return Error::io_failure;
  }

  // 逻辑尺寸 = 面板尺寸：渲染时由 SDL 把 320×240 放大到窗口，后端不用自己算。
  if (SDL_RenderSetLogicalSize(renderer_, static_cast<int>(display_width),
                               static_cast<int>(display_height)) != 0) {
    ELOG_ERROR("显示：SDL_RenderSetLogicalSize 失败（{}）", SDL_GetError());
    destroy();
    return Error::io_failure;
  }

  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                               static_cast<int>(display_width), static_cast<int>(display_height));
  if (texture_ == nullptr) {
    ELOG_ERROR("显示：SDL_CreateTexture 失败（{}）", SDL_GetError());
    destroy();
    return Error::io_failure;
  }

  ready_ = true;
  ELOG_INFO("显示：SDL 窗口 {}×{}（放大 {}×，纹理 RGB565）", display_width, display_height,
            window_scale_);
  return Error::none;
}

bool HostDisplay::is_ready() const noexcept {
  return ready_;
}

hal::DisplayInfo HostDisplay::info() const noexcept {
  if (!ready_) {
    return hal::DisplayInfo{};  // 未初始化 → 零值结构（HAL 契约）
  }
  hal::DisplayInfo value;
  value.width = static_cast<std::uint16_t>(display_width);
  value.height = static_cast<std::uint16_t>(display_height);
  value.format = hal::PixelFormat::rgb565;
  value.stride_bytes = static_cast<std::uint16_t>(display_stride_bytes);
  return value;
}

Error HostDisplay::flush(const hal::Rect& area, etl::span<const std::uint8_t> data) noexcept {
  if (!ready_) {
    return Error::not_ready;
  }
  if (!hal::is_valid(area)) {
    return Error::invalid_argument;
  }

  const hal::DisplayInfo panel = info();
  if (panel.format != hal::PixelFormat::rgb565) {
    return Error::unsupported;  // 本后端只做 RGB565（与 lv_conf.h 的 LV_COLOR_DEPTH 一致）
  }
  if (area.x < 0 || area.y < 0 || area.x + area.width > panel.width ||
      area.y + area.height > panel.height) {
    return Error::invalid_argument;
  }

  // HAL 的 flush 数据是"紧凑"的：每行 area.width 个像素，没有行尾填充。
  const std::size_t row_bytes = static_cast<std::size_t>(area.width) * sizeof(std::uint16_t);
  const std::size_t needed = row_bytes * static_cast<std::size_t>(area.height);
  if (data.size() < needed) {
    return Error::invalid_argument;
  }

  const void* source = data.data();
  if (backlight_ < full_backlight) {
    if (!ensure_staging(needed)) {
      return Error::no_space;
    }
    auto* destination = reinterpret_cast<std::uint16_t*>(staging_.get());
    for (std::size_t index = 0; index < needed / sizeof(std::uint16_t); ++index) {
      const std::uint16_t pixel =
          static_cast<std::uint16_t>(data[index * 2U]) |
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[index * 2U + 1U]) << 8U);
      destination[index] = scale_rgb565(pixel, backlight_);
    }
    source = staging_.get();
  }

  const SDL_Rect target{area.x, area.y, area.width, area.height};
  if (SDL_UpdateTexture(texture_, &target, source, static_cast<int>(row_bytes)) != 0) {
    ELOG_ERROR("显示：SDL_UpdateTexture 失败（{}）", SDL_GetError());
    return Error::io_failure;
  }

  SDL_RenderClear(renderer_);
  if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) {
    ELOG_ERROR("显示：SDL_RenderCopy 失败（{}）", SDL_GetError());
    return Error::io_failure;
  }
  SDL_RenderPresent(renderer_);
  ++presents_;
  return Error::none;
}

Error HostDisplay::set_backlight(std::uint8_t percent) noexcept {
  if (percent > full_backlight) {
    return Error::invalid_argument;
  }
  backlight_ = percent;
  return Error::none;
}

SDL_Surface* HostDisplay::capture_surface() noexcept {
  if (!ready_) {
    return nullptr;
  }

  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(renderer_, &width, &height) != 0 || width <= 0 || height <= 0) {
    return nullptr;
  }

  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (surface == nullptr) {
    return nullptr;
  }

  // 顺序是这套截图的关键（见头文件说明）：整屏重画 → 读后备缓冲 → 才 Present。
  SDL_RenderClear(renderer_);
  if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0 ||
      SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels,
                           surface->pitch) != 0) {
    SDL_FreeSurface(surface);
    return nullptr;
  }
  SDL_RenderPresent(renderer_);
  ++presents_;
  return surface;
}

bool HostDisplay::ensure_staging(std::size_t bytes) noexcept {
  if (staging_bytes_ >= bytes) {
    return true;
  }
  // nothrow：本函数是 noexcept，失败要变成返回值而不是 terminate。
  auto buffer = std::unique_ptr<std::uint8_t[]>(new (std::nothrow) std::uint8_t[bytes]);
  if (!buffer) {
    ELOG_ERROR("显示：背光暂存区分配 {} 字节失败", bytes);
    return false;
  }
  staging_ = std::move(buffer);
  staging_bytes_ = bytes;
  return true;
}

void HostDisplay::destroy() noexcept {
  ready_ = false;
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if (owns_video_) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    owns_video_ = false;
  }
}

}  // namespace embark::platform::host
