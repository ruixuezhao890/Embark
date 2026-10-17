/**
 * Embark ESP32-S3 · 时间后端（issues/11）
 *
 * 映射（spec §8 的 ITime 三件事）：
 *   - now_ms()  → esp_timer_get_time() / 1000，单调、微秒精度、不受系统时间影响；
 *   - delay_ms() → vTaskDelay(pdMS_TO_TICKS(ms))，让出 CPU（绝不忙等）；
 *   - epoch_ms() → 板载 RTC PCF85063（I2C_NUM_1，0x51）开机读一次，之后用 now_ms() 的
 *     差值往前推（2026-10-17 接上，此前恒为 unsupported）。
 *
 * epoch_ms() 的可用性不是"常量"而是"开机时探出来的"：
 *   * 芯片不在总线上 / 没有板载总线（宿主那种装配）→ unsupported；
 *   * 芯片在但 OS 位为 1（电池没电、时钟停过）→ **用固件编译时刻播种一次**（2026-10-17 定的
 *     策略，见 seed_from_build_time）：写时间 + 清 OS 位，然后按"有值"处理；播种也失败才 unsupported；
 *   * 有值 → 返回 UTC epoch 毫秒（本地时区由 App 层加，见 app/common/local_clock.h）。
 * 播种来的时间不是权威时间：误差 = 编译到上电那段（分钟级），没有纽扣电池时每次开机都会重播。
 * 按契约 unsupported 不是致命错误，调用方（时钟 App / launcher）必须自己处理。
 */
#ifndef EMBARK_PLATFORM_ESP32_TIME_H
#define EMBARK_PLATFORM_ESP32_TIME_H

#include <cstdint>

#include <embark/error.h>
#include <embark/hal/bus.h>
#include <embark/hal/time.h>
#include <middleware/etl/expected.h>

#include "esp32_rtc.h"

namespace embark::platform::esp32 {

class Esp32Time final : public hal::ITime {
 public:
  Esp32Time() noexcept = default;
  Esp32Time(const Esp32Time&) = delete;
  Esp32Time& operator=(const Esp32Time&) = delete;
  ~Esp32Time() override = default;

  /**
   * 幂等。now_ms/delay_ms 不需要初始化（esp_timer 在 IDF 启动时就跑起来了）；
   * 这里只做一件事：把板载 RTC 挂上总线并读一次，成功则把墙钟锚在本刻的 now_ms() 上。
   *
   * bus 可以为 nullptr（这台机器没有板载总线），那就不碰 RTC，epoch_ms() 如实报 unsupported。
   * **任何 RTC 侧的失败都只记日志、返回 none**：时间不可用不该挡住整个固件启动。
   */
  [[nodiscard]] Error init(hal::IBus* bus) noexcept;

  [[nodiscard]] std::uint32_t now_ms() const noexcept override;
  void delay_ms(std::uint32_t ms) noexcept override;
  [[nodiscard]] etl::expected<std::uint64_t, Error> epoch_ms() const noexcept override;

  /**
   * 手动设表：写板载 RTC（PCF85063）再重锚，成功后 epoch_ms() 立刻反映新值。
   *
   * 三条边界：
   *   * 芯片没应答（没有 RTC 的装配）→ unsupported，**不写**任何东西；
   *   * 芯片只有秒级精度：毫秒被截到秒，锚点用**回读**的秒（芯片真认下的值），不用入参；
   *   * 写失败/回读不符 → 原样返回驱动给的错误，wall_note_ 改成"手动设置失败"。
   * 不碰 0x02 的 offset 校准寄存器（这块板没有温度参考，动了只会引入人为偏差）。
   */
  Error set_epoch_ms(std::uint64_t epoch_ms) noexcept override;

  // --- 诊断（日志与心跳用；不参与 HAL 契约）-----------------------------------
  /// 墙钟可用吗（= 开机时 RTC 探到了、OS 位为 0、BCD 合法）。
  [[nodiscard]] bool wall_clock_ready() const noexcept { return wall_ready_; }
  /// 一句话说清墙钟状态（"已对时" / "0x51 无应答" / "时钟停过（OS 位=1）"……），常驻字符串。
  [[nodiscard]] const char* wall_clock_note() const noexcept { return wall_note_; }
  /// RTC 芯片应答过吗。
  [[nodiscard]] bool rtc_present() const noexcept { return rtc_.present(); }

 private:
  /**
   * 芯片报 OS=1（或读不出时间）时，用 EMBARK_BUILD_EPOCH（cmake 配置时刻，UTC 秒）写一次，
   * 成功则把墙钟锚在本刻。板级开关见 esp32_board.h 的 rtc_seed_from_build_time。
   * 失败只改 wall_note_，不抛错 —— 时间不可用不该挡启动。
   */
  void seed_from_build_time() noexcept;

  Pcf85063 rtc_{};
  bool wall_ready_ = false;
  const char* wall_note_ = "未探测";
  /// 锚点：读 RTC 那一刻的墙钟（UTC 毫秒）与当时的 now_ms()。
  std::uint64_t base_epoch_ms_ = 0;
  std::uint32_t base_now_ms_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_TIME_H */
