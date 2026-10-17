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
#include <cstdint>

#include <embark/hal/time.h>

namespace embark::platform::host {

class HostTime final : public hal::ITime {
 public:
  HostTime() noexcept : start_(std::chrono::steady_clock::now()) {}

  [[nodiscard]] std::uint32_t now_ms() const noexcept override;

  void delay_ms(std::uint32_t ms) noexcept override;

  [[nodiscard]] etl::expected<std::uint64_t, Error> epoch_ms() const noexcept override;

  /// 宿主没有 RTC，也不需要碰系统时间（那要管理员权限）：改成记一份偏移量，
  /// epoch_ms() = 系统时间 + 偏移。于是"设表"只影响本进程看到的墙钟。
  Error set_epoch_ms(std::uint64_t epoch_ms) noexcept override;

 private:
  std::chrono::steady_clock::time_point start_;
  std::int64_t epoch_offset_ms_ = 0;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_TIME_H */
