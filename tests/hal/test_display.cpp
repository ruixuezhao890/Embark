// HAL 显示能力：成功路径 + 失败路径（未初始化、非法区域、背光越界、init 故障）。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

using embark::Error;
using embark::fakes::FakeDisplay;

TEST_CASE("IDisplay：未 init 就 flush 返回 not_ready，且不记任何一帧") {
  FakeDisplay display;
  const std::uint8_t pixels[8] = {};

  CHECK_FALSE(display.is_ready());
  CHECK(display.flush(embark::hal::Rect{0, 0, 2, 2},
                      etl::span<const std::uint8_t>(pixels, sizeof(pixels))) == Error::not_ready);
  CHECK(display.flush_count == 0U);
  CHECK(display.total_bytes == 0U);
}

TEST_CASE("IDisplay：init 后 flush 记下区域与字节数") {
  FakeDisplay display;
  REQUIRE(display.init() == Error::none);
  CHECK(display.is_ready());

  const embark::hal::DisplayInfo info = display.info();
  CHECK(info.width == 320U);
  CHECK(info.height == 240U);
  CHECK(info.format == embark::hal::PixelFormat::rgb565);
  CHECK(info.stride_bytes == 640U);
  CHECK(embark::hal::bits_per_pixel(info.format) == 16U);

  const embark::hal::Rect area{4, 6, 2, 2};
  CHECK(embark::hal::area_bytes(area, info.format) == 8U);

  const std::uint8_t pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(display.flush(area, etl::span<const std::uint8_t>(pixels, sizeof(pixels))) == Error::none);

  CHECK(display.flush_count == 1U);
  CHECK(display.last_area.x == 4);
  CHECK(display.last_area.y == 6);
  CHECK(display.last_area.width == 2);
  CHECK(display.last_area.height == 2);
  CHECK(display.last_pixel_bytes == 8U);
  CHECK(display.total_bytes == 8U);
  CHECK(display.last_pixels[7] == 8U);
  CHECK_FALSE(display.truncated_capture);

  CHECK(display.set_backlight(70U) == Error::none);
  CHECK(display.backlight_percent == 70U);
  CHECK(display.backlight_calls == 1U);
}

TEST_CASE("IDisplay：空/负区域与 >100 的背光都是 invalid_argument") {
  FakeDisplay display;
  REQUIRE(display.init() == Error::none);
  const std::uint8_t pixels[4] = {};

  CHECK(display.flush(embark::hal::Rect{0, 0, 0, 4},
                      etl::span<const std::uint8_t>(pixels, sizeof(pixels))) ==
        Error::invalid_argument);
  CHECK(display.flush(embark::hal::Rect{0, 0, 4, -1},
                      etl::span<const std::uint8_t>(pixels, sizeof(pixels))) ==
        Error::invalid_argument);
  CHECK(display.set_backlight(101U) == Error::invalid_argument);

  CHECK(display.flush_count == 0U);
  CHECK(display.backlight_calls == 0U);
}

TEST_CASE("IDisplay：init 故障原样透传，且保持未就绪") {
  FakeDisplay display;
  display.init_error = Error::io_failure;

  CHECK(display.init() == Error::io_failure);
  CHECK_FALSE(display.is_ready());
  CHECK(display.init_calls == 1U);
}
