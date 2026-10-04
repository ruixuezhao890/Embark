/**
 * Embark ESP32-S3 · 触摸输入后端（issues/11）
 *
 * 板载是 CST328 电容触摸：寄存器 16 位、I2C 从地址 0x1A。这里不走 esp_lcd_touch
 * 组件（那是给 LVGL 自带驱动用的），而是直连新 I2C 主驱动 —— 因为 HAL 要的是
 * "原始事件"，坐标变换留给框架/LVGL 那一层。
 */
#ifndef EMBARK_PLATFORM_ESP32_INPUT_H
#define EMBARK_PLATFORM_ESP32_INPUT_H

#include <cstddef>
#include <cstdint>

#include <embark/hal/input.h>

#include <driver/i2c_master.h>

namespace embark::platform::esp32 {

/**
 * CST328 触摸（单点）。
 *
 * 只上报 **按下的位置变化**：
 *   - 第一帧有触摸      → press
 *   - 按住且位置变了    → move（位置没变就不发事件，省掉框架侧的去重）
 *   - 从有到无          → release（坐标用最后按下的位置）
 * 坐标在读取时就按 `esp32_board.h` 的换轴/镜像开关换算好，再夹到屏幕范围内。
 */
class Esp32Input final : public hal::IInput {
 public:
  Esp32Input() noexcept = default;
  Esp32Input(const Esp32Input&) = delete;
  Esp32Input& operator=(const Esp32Input&) = delete;
  ~Esp32Input() override;

  [[nodiscard]] Error init() noexcept override;
  [[nodiscard]] etl::expected<bool, Error> poll(hal::InputEvent& event) noexcept override;

  // --- 实机诊断（不上报给框架，只给启动日志和 bring-up 用）---------------------
  /// 按下/抬起次数（用来判断"手在板子上划了半天却一次都没按下"这类问题）。
  [[nodiscard]] std::uint32_t presses() const noexcept { return presses_; }
  [[nodiscard]] std::uint32_t releases() const noexcept { return releases_; }
  /// I2C 读取失败次数（含超时）。频繁非零 = 总线接触/上拉/速率有问题。
  [[nodiscard]] std::uint32_t read_failures() const noexcept { return read_failures_; }

 private:
  /// 读寄存器（写 2 字节寄存器地址 + repeated start 再读）。
  [[nodiscard]] Error read_registers(std::uint16_t reg, std::uint8_t* buffer,
                                     std::size_t size) noexcept;
  /// 读一拍：`touched` = 这一拍有没有手指；坐标已换算并夹好。
  [[nodiscard]] Error read_point(std::uint16_t& x, std::uint16_t& y, bool& touched) noexcept;
  /// 往 0xD005 写 0 清中断（读点前后都要做，否则中断不再来）。
  void clear_interrupt() noexcept;

  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  bool bus_owned_ = false;

  std::uint32_t last_poll_ms_ = 0;
  bool pressed_ = false;
  std::uint16_t pressed_x_ = 0;
  std::uint16_t pressed_y_ = 0;

  std::uint32_t presses_ = 0;
  std::uint32_t releases_ = 0;
  std::uint32_t read_failures_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_INPUT_H */
