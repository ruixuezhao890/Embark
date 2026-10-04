#include "host_time.h"

#include <thread>

namespace embark::platform::host {

std::uint32_t HostTime::now_ms() const noexcept {
  const auto elapsed = std::chrono::steady_clock::now() - start_;
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void HostTime::delay_ms(std::uint32_t ms) noexcept {
  // 睡多久是"至少"：Windows 的定时器粒度会让它更长（issue 02 量到 Sleep(1)≈2 ms）。
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

etl::expected<std::uint64_t, Error> HostTime::epoch_ms() const noexcept {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count());
}

}  // namespace embark::platform::host
