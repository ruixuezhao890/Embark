/**
 * Embark ESP32-S3 · 显示后端（issues/11）
 *
 * 面板：240×320 原生竖屏的 ST7789，走 SPI3（80 MHz）。
 * 对外口径：**竖屏 240×320 RGB565**（与 config/embark_limits.h 一致）——正是面板原生
 * 方向，所以方向开关取厂商旋转表的 ROT_NONE 配方（swap_xy=false, mirror=(true,false)）。
 *
 * 两个被官方资料钉死的细节（都写在 esp32_board.h 的开关里，改板只动那里）：
 *   - 颜色分量顺序：BGR（厂商 IDF demo 的 `LCD_RGB_ENDIAN_BGR`）；
 *   - 16 位像素的字节序：小端（厂商初始化里的 0xB0=0x00,0xE8，bit3 就是小端位）。
 *     这条正好与 LV_COLOR_16_SWAP=0 的 LVGL 缓冲一致 —— 不需要搬一次字节。
 *
 * 初始化序列：IDF 自带 esp_lcd 的 ST7789 驱动负责 0x11/0x36/0x3A/0xB0（RAMCTRL，
 * 含上面的字节序设置），面板厂那一串电源/伽马（0xB2/0xB7/0xBB/0xC0/0xC2/0xC3/
 * 0xC4/0xC6/0xD0/0xE0/0xE1）在它之后补发，0x21（反色）与 0x29（开显示）交给
 * esp_lcd 的 invert_color() / disp_on_off()，避免两处设置打架。
 *
 * 刷新语义：flush() 是**同步**的（HAL 契约要求返回时缓冲即可复用）。
 * esp_lcd 的 draw_bitmap 只是入队，完成回调只在最后一个 DMA chunk 之后触发
 * （见 IDF 的 esp_lcd_panel_io_spi.c：`en_trans_done_cb` 只在最后一笔置位），
 * 所以这里用"等一个二值信号量"就能准确等到整块送完。
 */
#ifndef EMBARK_PLATFORM_ESP32_DISPLAY_H
#define EMBARK_PLATFORM_ESP32_DISPLAY_H

#include <cstddef>
#include <cstdint>

#include <embark/hal/display.h>

#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>

namespace embark::platform::esp32 {

class Esp32Display final : public hal::IDisplay {
 public:
  Esp32Display() noexcept = default;
  Esp32Display(const Esp32Display&) = delete;
  Esp32Display& operator=(const Esp32Display&) = delete;
  ~Esp32Display() override;

  /// SPI 总线 + panel IO + ST7789 + 背光 LEDC。幂等。
  [[nodiscard]] Error init() noexcept override;
  [[nodiscard]] bool is_ready() const noexcept override { return ready_; }
  [[nodiscard]] hal::DisplayInfo info() const noexcept override;
  Error flush(const hal::Rect& area, etl::span<const std::uint8_t> data) noexcept override;
  Error set_backlight(std::uint8_t percent) noexcept override;

  // --- 观测（bring-up 与验收用；不属于 HAL 契约）-----------------------------
  /// 成功完成的刷新次数。
  [[nodiscard]] std::uint32_t refreshes() const noexcept { return refreshes_; }
  /// 累计送上屏的字节数。
  [[nodiscard]] std::size_t flushed_bytes() const noexcept { return flushed_bytes_; }
  /// 等待 DMA 完成的超时次数（正常应当恒为 0）。
  [[nodiscard]] std::uint32_t flush_timeouts() const noexcept { return flush_timeouts_; }

 private:
  /// DMA 完成回调（在 SPI 中断里跑）：给信号量，并告诉调用方是否要切任务。
  static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t* event, void* context);

  /// 面板厂的电源/伽马初始化串（不含 IDF 已经设过的 0x11/0x21/0x29/0x36/0x3A/0xB0）。
  [[nodiscard]] Error send_vendor_sequence() noexcept;

  [[nodiscard]] Error init_backlight() noexcept;

  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  bool bus_owned_ = false;  // SPI 总线是这次 init 建起来的（析构时归还）
  bool backlight_ready_ = false;
  bool ready_ = false;
  std::uint8_t backlight_percent_ = 0;
  std::uint32_t refreshes_ = 0;
  std::size_t flushed_bytes_ = 0;
  std::uint32_t flush_timeouts_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_DISPLAY_H */
