/**
 * HAL · 时间（spec §8）
 *
 * 一个反直觉但必须守住的边界：宿主上"1 tick = 1 ms"并不成立 —— Windows 定时器粒度
 * 把 FreeRTOS 的一拍拉到约 2 ms（issue 02 实测）。所以任何"多久之后"的断言都只能比
 * tick 相对量，不能比墙钟（spec §13）。
 */
#ifndef EMBARK_HAL_TIME_H
#define EMBARK_HAL_TIME_H

#include <cstdint>

#include <embark/error.h>
#include <middleware/etl/expected.h>

namespace embark::hal {

class ITime {
 public:
  virtual ~ITime() = default;

  /// 单调递增毫秒。不受系统时间调整影响，从后端初始化起算（真机 = tick 计数换算）。
  [[nodiscard]] virtual std::uint32_t now_ms() const noexcept = 0;

  /// 让出 CPU 至少 ms 毫秒（真机 = vTaskDelay，宿主 = sleep）。
  virtual void delay_ms(std::uint32_t ms) noexcept = 0;

  /// 墙上时间（epoch 毫秒）。没有 RTC / 没对时 → Error::unsupported（不是致命错误）。
  [[nodiscard]] virtual etl::expected<std::uint64_t, Error> epoch_ms() const noexcept = 0;

  /// 把墙上钟设为 epoch 毫秒（UTC）。
  ///
  /// 成功之后 epoch_ms() 必须立刻反映新值；没有可写的时钟源（没有 RTC 的装配）→
  /// Error::unsupported，写不进去（总线 NACK、回读不符）→ 具体错误。
  /// **不许静默丢弃**：上层要能据此显示"没设成功"，而不是以为拨好了。
  ///
  /// 精度由后端决定：真机 RTC 只有秒级，传入的毫秒会被截到秒再回读锚定。
  virtual Error set_epoch_ms(std::uint64_t epoch_ms) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_TIME_H */
