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
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_TIME_H */
