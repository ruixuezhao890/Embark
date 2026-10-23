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
 *   - 确认按下          → press
 *   - 按住且位置变了    → move（位置没变就不发事件，省掉框架侧的去重）
 *   - 确认抬起          → release（坐标用最后按下的位置）
 *
 * "有没有手指"取自 0xD000 报文的**第 0 字节**，不看 0xD005 的点数：CST328 手指静止时会报
 * "0 个触点"（那是"没有新报告"），而报文里的手指标志在整个按住期间恒为非 0、抬手当即归 0
 * （2026-10-17 真机实测 799 条探测样本）。所以抬起只要 1 拍确认，不会再把一次按住切成几十次
 * 点击，也不会为此多等 60 ms。两个边沿各留 touch_press/release_confirm_hits 拍挡单拍杂点。
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
  /// 滑动次数（按住期间上报的位置变化数）。
  [[nodiscard]] std::uint32_t moves() const noexcept { return moves_; }
  /// 按住期间被去抖窗口吞掉的空拍数：这个数远大于 presses() 才说明面板在"静止时报 0 点"。
  [[nodiscard]] std::uint32_t swallowed_gaps() const noexcept { return swallowed_gaps_; }
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

  /// 连续"有手指"/"没手指"的拍数：确认按下、确认抬起各要够 touch_*_confirm_hits 拍，
  /// 单拍杂点（总线抖一下、状态切换那一拍里的半旧数据）凑不够，不会变成一次点击。
  std::uint8_t press_streak_ = 0;
  std::uint8_t release_streak_ = 0;

  /// 还没确认按下时，**最早**见手指的那一拍。按下事件报这个坐标而不是"确认那一刻"的坐标：
  /// 确认要等 2 拍（10 ms），这段时间手指已经移开一截，报当前位置会让 LVGL 只看到最后那一
  /// 小段位移（真机实测快速轻划只剩 3 px），攒不够滚动阈值 → 滑动被当成点击。
  std::uint16_t pending_x_ = 0;
  std::uint16_t pending_y_ = 0;
  bool pending_valid_ = false;

  std::uint32_t presses_ = 0;
  std::uint32_t releases_ = 0;
  std::uint32_t moves_ = 0;
  std::uint32_t swallowed_gaps_ = 0;
  std::uint32_t read_failures_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_INPUT_H */
