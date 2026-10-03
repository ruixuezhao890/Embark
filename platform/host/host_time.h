/**
 * 宿主 · 时间后端（issues/04）
 *
 * 用 steady_clock（单调）做 now_ms，用 system_clock 做 epoch_ms。
 * 注意：宿主上 FreeRTOS 的一拍约等于 2 ms（issue 02 实测），所以这一层给的是"真实
 * 毫秒"，而内核里的 tick 是另一条时间轴 —— 两者只在实测里对齐，不要互相假设。
 */
#ifndef EMBARK_PLATFORM_HOST_TIME_H
#define EMBARK_PLATFORM_HOST_TIME_H

#include <chrono>

#include <embark/hal/time.h>

namespace embark::platform::host {

class HostTime final : public hal::ITime {
 public:
  HostTime() noexcept : start_(std::chrono::steady_clock::now()) {}

  [[nodiscard]] std::uint32_t now_ms() const noexcept override;

  void delay_ms(std::uint32_t ms) noexcept override;

  [[nodiscard]] etl::expected<std::uint64_t, Error> epoch_ms() const noexcept override;

 private:
  std::chrono::steady_clock::time_point start_;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_TIME_H */
