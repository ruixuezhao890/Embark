/**
 * 宿主 · 显示后端（SDL2，issues/05）
 *
 * 一个窗口、一张 RGB565 streaming 纹理：HAL 的 flush(区域, 紧凑像素) 直接
 * → SDL_UpdateTexture（pitch = 区域宽 × 2，与 HAL 的紧凑布局逐字对应）
 * → SDL_RenderCopy 到逻辑尺寸（窗口按 window_scale 放大，最近邻取样，不糊）
 * → SDL_RenderPresent。
 *
 * 为什么不做双缓冲/脏区合并：LVGL 自己就有 draw buffer 与脏区，显示后端再做一层只会
 * 多一次拷贝；HAL 的 flush 契约就是"返回时缓冲区可复用"，同步刷完即可。
 *
 * 背光：百分比直接乘到像素上（RGB565 逐通道缩放），与真机背光 PWM 的观感一致；
 * 100% 时走零拷贝直通路径，不碰暂存区。
 *
 * 线程：SDL 的窗口/渲染/事件都必须在同一个线程上碰，宿主就是"UI 任务"那一条线（spec §5）。
 */
#ifndef EMBARK_PLATFORM_HOST_DISPLAY_H
#define EMBARK_PLATFORM_HOST_DISPLAY_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include <SDL.h>

#include <embark/error.h>
#include <embark/hal/display.h>

namespace embark::platform::host {

class HostDisplay final : public hal::IDisplay {
 public:
  /// title 必须比本对象活得久（传字面量即可）。
  explicit HostDisplay(std::uint8_t window_scale = 2, const char* title = "Embark 宿主") noexcept;
  ~HostDisplay() noexcept override;

  HostDisplay(const HostDisplay&) = delete;
  HostDisplay& operator=(const HostDisplay&) = delete;

  [[nodiscard]] Error init() noexcept override;
  [[nodiscard]] bool is_ready() const noexcept override;
  [[nodiscard]] hal::DisplayInfo info() const noexcept override;
  [[nodiscard]] Error flush(const hal::Rect& area,
                            etl::span<const std::uint8_t> data) noexcept override;
  [[nodiscard]] Error set_backlight(std::uint8_t percent) noexcept override;

  /// 渲染器：宿主 UI 循环/截图要用（SDL_RenderReadPixels）。未初始化时是 nullptr。
  [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }

  /// 窗口：宿主 UI 循环用它判断"窗口还在不在"。
  [[nodiscard]] SDL_Window* window() const noexcept { return window_; }

  /// 一次成功 flush 之后是否真的 Present 过（验收用：证明画面到过窗口）。
  [[nodiscard]] std::size_t presents() const noexcept { return presents_; }

  /// 把整张纹理重画到后备缓冲、读成一个 32 位 ARGB 表面（调用方负责 SDL_FreeSurface）。
  ///
  /// 截图必须走这里：正常 flush 只重画 LVGL 脏区，而 SDL_RenderReadPixels 读的是后备缓冲
  /// ——在部分驱动的 flip 模型下，Present 之后那块缓冲的内容是未定义的（实测第一版
  /// 截图有一大片没画上的黑区）。所以这里是"先整屏重画 → Present 之前读数 → 再 Present"，
  /// 纹理里始终存着整帧（每个脏区都写进同一张纹理），读出来的就是窗口当时真正的样子。
  /// 失败返回 nullptr（SDL_GetError 里有原因）。
  [[nodiscard]] SDL_Surface* capture_surface() noexcept;

 private:
  [[nodiscard]] bool ensure_staging(std::size_t bytes) noexcept;
  void destroy() noexcept;

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  const char* title_;
  std::uint8_t window_scale_;
  std::uint8_t backlight_ = 100;
  bool ready_ = false;
  bool owns_video_ = false;  ///< SDL_Init 是不是我们做的（决定析构要不要 SDL_QuitSubSystem）
  std::size_t presents_ = 0;

  /// 背光缩放用的暂存区：只在 backlight_ < 100 时按需增长（100% 直通不分配）。
  std::unique_ptr<std::uint8_t[]> staging_;
  std::size_t staging_bytes_ = 0;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_DISPLAY_H */
