// 测试替身：假显示后端。
//
// 不画任何东西，只记录"谁被要求画了哪块区域、多少字节"，以及背光百分比。
#pragma once

#include <embark/hal/display.h>
#include <embark_limits.h>

#include <cstddef>
#include <cstdint>

namespace embark::fakes {

class FakeDisplay final : public hal::IDisplay {
 public:
  FakeDisplay() noexcept = default;

  Error init() noexcept override {
    ++init_calls;
    if (init_error != Error::none) {
      return init_error;
    }
    ready = true;
    return Error::none;
  }

  [[nodiscard]] bool is_ready() const noexcept override { return ready; }

  [[nodiscard]] hal::DisplayInfo info() const noexcept override { return info_value; }

  Error flush(const hal::Rect& area, etl::span<const std::uint8_t> pixels) noexcept override {
    if (!ready) {
      return Error::not_ready;
    }
    if (!hal::is_valid(area)) {
      return Error::invalid_argument;
    }
    ++flush_count;
    last_area = area;
    last_pixel_bytes = pixels.size();
    total_bytes += pixels.size();
    // 只留前 64 字节做指纹：假后端不关心整屏像素，超出的部分仅计数。
    const std::size_t kept =
        pixels.size() < last_pixels.size() ? pixels.size() : last_pixels.size();
    if (kept < pixels.size()) {
      truncated_capture = true;
    }
    for (std::size_t i = 0; i < kept; ++i) {
      last_pixels[i] = pixels[i];
    }
    return Error::none;
  }

  Error set_backlight(std::uint8_t percent) noexcept override {
    if (percent > 100U) {
      return Error::invalid_argument;
    }
    backlight_percent = percent;
    ++backlight_calls;
    return Error::none;
  }

  // 旋钮与观测点。
  Error init_error = Error::none;
  // 默认面板尺寸直接引用框架的编译期常量：改分辨率只需要动 config/embark_limits.h。
  hal::DisplayInfo info_value{static_cast<std::uint16_t>(embark::display_width),
                              static_cast<std::uint16_t>(embark::display_height),
                              hal::PixelFormat::rgb565,
                              static_cast<std::uint16_t>(embark::display_stride_bytes)};
  std::size_t init_calls = 0;
  bool ready = false;
  std::size_t flush_count = 0;
  std::size_t total_bytes = 0;
  hal::Rect last_area{0, 0, 0, 0};
  std::size_t last_pixel_bytes = 0;
  etl::array<std::uint8_t, 64> last_pixels{};
  bool truncated_capture = false;
  std::uint8_t backlight_percent = 0;
  std::size_t backlight_calls = 0;
};

}  // namespace embark::fakes
