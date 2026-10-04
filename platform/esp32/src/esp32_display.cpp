/**
 * Embark ESP32-S3 · 显示后端实现（issues/11）
 */
#include "esp32_display.h"

#include <embark/error.h>
#include <embark_limits.h>
#include <middleware/elog/elog.hpp>

#include <esp_err.h>

#include "esp32_board.h"

namespace embark::platform::esp32 {
namespace {

/// 等一次 DMA 完成的上限。80 MHz 下单次 flush 最多 40 行（25.6 KB ≈ 2.6 ms），
/// 100 ms 已经等于"SPI 卡住了"。
constexpr std::uint32_t flush_wait_ms = 100;

/// DMA 完成信号量：静态存储（零堆），DMA 中断给、UI 线程取。
StaticSemaphore_t flush_semaphore_storage = {};
SemaphoreHandle_t flush_semaphore = nullptr;

/// 面板厂的电源/伽马初始化串。
/// 只放 IDF 驱动没设的寄存器：
///   0x11 SLPOUT / 0x36 MADCTL / 0x3A COLMOD / 0xB0 RAMCTRL 都在 esp_lcd_panel_init() 里；
///   0x21 INVON 用 esp_lcd_panel_invert_color()，0x29 DISPON 用 disp_on_off()。
/// 数值取自厂商 IDF demo（Vernon_ST7789T.c），不是自己配的。
struct VendorCommand {
  std::uint8_t command;
  std::uint8_t size;
  std::uint8_t data[14];
};

constexpr VendorCommand vendor_sequence[] = {
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},  // porch 设置
    {0xB7, 1, {0x75}},                          // VGH=14.97V, VGL=-7.67V
    {0xBB, 1, {0x1A}},                          // VCOM
    {0xC0, 1, {0x80}},                          // power control
    {0xC2, 2, {0x01, 0xFF}},                    // VDV/VRH
    {0xC3, 1, {0x13}},                          // VRH
    {0xC4, 1, {0x20}},                          // VDV
    {0xC6, 1, {0x0F}},                          // 帧率（60 Hz 档）
    {0xD0, 2, {0xA4, 0xA1}},                    // 电源控制
    {0xE0,
     14,
     {0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}},
    {0xE1,
     14,
     {0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x2E}},
};

constexpr std::size_t vendor_sequence_count = sizeof(vendor_sequence) / sizeof(vendor_sequence[0]);

}  // namespace

Esp32Display::~Esp32Display() {
  ready_ = false;
  if (backlight_ready_) {
    (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    (void)ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    backlight_ready_ = false;
  }
  if (panel_ != nullptr) {
    (void)esp_lcd_panel_disp_on_off(panel_, false);
    (void)esp_lcd_panel_del(panel_);
    panel_ = nullptr;
  }
  if (io_ != nullptr) {
    (void)esp_lcd_panel_io_del(io_);
    io_ = nullptr;
  }
  if (bus_owned_) {
    (void)spi_bus_free(SPI3_HOST);
    bus_owned_ = false;
  }
  flush_semaphore = nullptr;  // 静态存储，标记失效即可
}

Error Esp32Display::init() noexcept {
  if (ready_) {
    return Error::none;  // 幂等
  }

  // 1) DMA 完成信号量（先建好：回调可能在第一次 flush 之前就注册上）。
  if (flush_semaphore == nullptr) {
    flush_semaphore = xSemaphoreCreateBinaryStatic(&flush_semaphore_storage);
    if (flush_semaphore == nullptr) {
      ELOG_ERROR("显示后端：二值信号量创建失败");
      return Error::no_space;
    }
  }

  // 2) SPI3 总线。一次能给的最大长度 = 整屏，这样单次 flush 就是一整笔 DMA，
  //    完成回调也只会有一次（IDF 只在最后一个 chunk 上置 en_trans_done_cb）。
  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = lcd_pin_mosi;
  bus_config.miso_io_num = -1;  // 只写不读
  bus_config.sclk_io_num = lcd_pin_sclk;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz =
      static_cast<int>(embark::display_stride_bytes * embark::display_height);

  esp_err_t error = spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (error == ESP_ERR_INVALID_STATE) {
    // 总线已经初始化过（同一固件里显示后端只该有一个）——接着用，不报错。
    ELOG_WARN("显示后端：SPI3 总线已初始化，沿用现有总线");
  } else if (error != ESP_OK) {
    ELOG_ERROR("spi_bus_initialize 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  } else {
    bus_owned_ = true;
  }

  // 3) panel IO：flags 全 0 就是"DC 低 = 命令、DC 高 = 数据"，正是 ST7789 的口径。
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = lcd_pin_cs;
  io_config.dc_gpio_num = lcd_pin_dc;
  io_config.spi_mode = 0;
  io_config.pclk_hz = static_cast<unsigned int>(lcd_pixel_clock_hz);
  io_config.trans_queue_depth = 4;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.on_color_trans_done = &Esp32Display::on_color_trans_done;
  io_config.user_ctx = this;

  error =
      esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI3_HOST), &io_config, &io_);
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_new_panel_io_spi 失败：{}", esp_err_to_name(error));
    io_ = nullptr;
    return Error::io_failure;
  }

  // 4) ST7789（IDF 自带驱动）+ 面板厂的补充序列 + 方向。
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = lcd_pin_rst;
  panel_config.rgb_ele_order =
      lcd_rgb_element_order_bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.data_endian =
      lcd_data_little_endian ? LCD_RGB_DATA_ENDIAN_LITTLE : LCD_RGB_DATA_ENDIAN_BIG;
  panel_config.bits_per_pixel = 16;

  error = esp_lcd_new_panel_st7789(io_, &panel_config, &panel_);
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_new_panel_st7789 失败：{}", esp_err_to_name(error));
    panel_ = nullptr;
    return Error::io_failure;
  }

  error = esp_lcd_panel_reset(panel_);  // 硬复位脉冲（RST 低 → 高）
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_reset 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }
  error = esp_lcd_panel_init(panel_);  // SLPOUT + MADCTL + COLMOD + RAMCTRL（含字节序）
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_init 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }

  const Error vendor_result = send_vendor_sequence();
  if (vendor_result != Error::none) {
    return vendor_result;
  }

  // 方向：厂商 LVGL 驱动的 ROT_90 配方（swap_xy + mirror(true,true)）。
  // 注意顺序 —— 这几个 API 都会重写 MADCTL，必须放在 init() 之后。
  error = esp_lcd_panel_swap_xy(panel_, lcd_swap_xy);
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_swap_xy 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }
  error = esp_lcd_panel_mirror(panel_, lcd_mirror_x, lcd_mirror_y);
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_mirror 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }
  error = esp_lcd_panel_invert_color(panel_, lcd_invert_color);
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_invert_color 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }
  error = esp_lcd_panel_disp_on_off(panel_, true);
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_disp_on_off 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }

  // 5) 背光（LEDC 13 位 / 5 kHz）。
  const Error backlight_result = init_backlight();
  if (backlight_result != Error::none) {
    ELOG_WARN("背光初始化失败：{}（画面还能用，只是亮度不可调）", to_string(backlight_result));
  }

  ready_ = true;
  if (backlight_ready_) {
    (void)set_backlight(backlight_default_percent);
  }

  ELOG_INFO("ST7789 就绪：{}×{} 横屏，SPI {} MHz，BGR={}，像素小端={}，反色={}",
            static_cast<unsigned>(embark::display_width),
            static_cast<unsigned>(embark::display_height),
            static_cast<unsigned>(lcd_pixel_clock_hz / 1000000U), lcd_rgb_element_order_bgr ? 1 : 0,
            lcd_data_little_endian ? 1 : 0, lcd_invert_color ? 1 : 0);
  return Error::none;
}

hal::DisplayInfo Esp32Display::info() const noexcept {
  hal::DisplayInfo result = {};
  result.width = static_cast<std::uint16_t>(embark::display_width);
  result.height = static_cast<std::uint16_t>(embark::display_height);
  result.format = hal::PixelFormat::rgb565;
  result.stride_bytes = static_cast<std::uint16_t>(embark::display_stride_bytes);
  return result;
}

Error Esp32Display::flush(const hal::Rect& area, etl::span<const std::uint8_t> data) noexcept {
  if (!ready_ || panel_ == nullptr || flush_semaphore == nullptr) {
    return Error::not_ready;
  }
  if (!hal::is_valid(area)) {
    return Error::invalid_argument;
  }
  const std::size_t expected = hal::area_bytes(area, hal::PixelFormat::rgb565);
  if (data.data() == nullptr || data.size() < expected) {
    return Error::invalid_argument;
  }

  // esp_lcd 用的是"左上闭、右下开"的矩形。
  const int x_start = static_cast<int>(area.x);
  const int y_start = static_cast<int>(area.y);
  const int x_end = static_cast<int>(area.x + area.width);
  const int y_end = static_cast<int>(area.y + area.height);
  if (x_start < 0 || y_start < 0 || x_end > static_cast<int>(embark::display_width) ||
      y_end > static_cast<int>(embark::display_height)) {
    return Error::invalid_argument;
  }

  const esp_err_t error =
      esp_lcd_panel_draw_bitmap(panel_, x_start, y_start, x_end, y_end, data.data());
  if (error != ESP_OK) {
    ELOG_ERROR("esp_lcd_panel_draw_bitmap 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }

  // draw_bitmap 只是入队；这里必须等到这一块的 DMA 真的结束 ——
  // HAL 的 flush 是同步语义，返回时调用方就可以复用缓冲了。
  if (xSemaphoreTake(flush_semaphore, pdMS_TO_TICKS(flush_wait_ms)) != pdTRUE) {
    ++flush_timeouts_;
    ELOG_WARN("显示刷新等待超时（{} ms）：SPI 传输未在预期内完成",
              static_cast<unsigned>(flush_wait_ms));
    return Error::timeout;
  }

  ++refreshes_;
  flushed_bytes_ += expected;
  return Error::none;
}

Error Esp32Display::set_backlight(std::uint8_t percent) noexcept {
  if (percent > 100U) {
    return Error::invalid_argument;
  }
  if (!backlight_ready_) {
    return Error::unsupported;  // 硬件上没有背光控制电路时就是这个答案
  }

  const std::uint32_t max_duty = (1U << static_cast<unsigned>(backlight_pwm_bits)) - 1U;
  const std::uint32_t duty = (static_cast<std::uint32_t>(percent) * max_duty) / 100U;

  const esp_err_t set = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  if (set != ESP_OK) {
    ELOG_WARN("ledc_set_duty 失败：{}", esp_err_to_name(set));
    return Error::io_failure;
  }
  const esp_err_t update = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  if (update != ESP_OK) {
    ELOG_WARN("ledc_update_duty 失败：{}", esp_err_to_name(update));
    return Error::io_failure;
  }

  backlight_percent_ = percent;
  return Error::none;
}

bool Esp32Display::on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                       esp_lcd_panel_io_event_data_t* event, void* context) {
  (void)io;
  (void)event;
  (void)context;
  if (flush_semaphore == nullptr) {
    return false;
  }
  // 在 SPI 中断里跑：只能用 FromISR 版本。
  BaseType_t higher_awoken = pdFALSE;
  (void)xSemaphoreGiveFromISR(flush_semaphore, &higher_awoken);
  return higher_awoken == pdTRUE;  // 返回值 = "是否唤醒了更高优先级任务"
}

Error Esp32Display::send_vendor_sequence() noexcept {
  for (std::size_t index = 0; index < vendor_sequence_count; ++index) {
    const VendorCommand& entry = vendor_sequence[index];
    const esp_err_t error = esp_lcd_panel_io_tx_param(io_, entry.command, entry.data, entry.size);
    if (error != ESP_OK) {
      ELOG_ERROR("面板初始化命令 0x{:02X} 发送失败：{}", static_cast<unsigned>(entry.command),
                 esp_err_to_name(error));
      return Error::io_failure;
    }
  }
  return Error::none;
}

Error Esp32Display::init_backlight() noexcept {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_13_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = backlight_pwm_hz;
  timer.clk_cfg = LEDC_AUTO_CLK;

  esp_err_t error = ledc_timer_config(&timer);
  if (error != ESP_OK) {
    ELOG_WARN("ledc_timer_config 失败：{}", esp_err_to_name(error));
    return Error::io_failure;
  }

  ledc_channel_config_t channel = {};
  channel.gpio_num = lcd_pin_backlight;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_0;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_0;
  channel.duty = 0;  // 先黑着：面板初始化完再点亮，免得看到花屏闪一下
  channel.hpoint = 0;

  error = ledc_channel_config(&channel);
  if (error != ESP_OK) {
    ELOG_WARN("ledc_channel_config(GPIO {}) 失败：{}", lcd_pin_backlight, esp_err_to_name(error));
    return Error::io_failure;
  }

  backlight_ready_ = true;
  return Error::none;
}

}  // namespace embark::platform::esp32
