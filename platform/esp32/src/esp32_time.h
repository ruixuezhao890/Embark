/**
 * Embark ESP32-S3 · 时间后端（issues/11）
 *
 * 映射（spec §8 的 ITime 三件事）：
 *   - now_ms()  → esp_timer_get_time() / 1000，单调、微秒精度、不受系统时间影响；
 *   - delay_ms() → vTaskDelay(pdMS_TO_TICKS(ms))，让出 CPU（绝不忙等）；
 *   - epoch_ms() → 暂时 unsupported：板上 RTC 是 PCF85063（I2C，0x51），
 *     v1 还没接（见 platform/esp32/README.md 的"待接清单"）。按契约这不算错，
 *     调用方必须自己处理 unsupported，而不是把 0 当成 1970 年。
 */
#ifndef EMBARK_PLATFORM_ESP32_TIME_H
#define EMBARK_PLATFORM_ESP32_TIME_H

#include <cstdint>

#include <embark/error.h>
#include <embark/hal/time.h>
#include <middleware/etl/expected.h>

namespace embark::platform::esp32 {

class Esp32Time final : public hal::ITime {
 public:
  Esp32Time() noexcept = default;
  Esp32Time(const Esp32Time&) = delete;
  Esp32Time& operator=(const Esp32Time&) = delete;
  ~Esp32Time() override = default;

  /// 幂等；本后端没有需要初始化的东西（esp_timer 在 IDF 启动时就已经跑起来了）。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] std::uint32_t now_ms() const noexcept override;
  void delay_ms(std::uint32_t ms) noexcept override;
  [[nodiscard]] etl::expected<std::uint64_t, Error> epoch_ms() const noexcept override;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_TIME_H */
