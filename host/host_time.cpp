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

namespace {

/// 系统时钟的 epoch 毫秒（未加偏移）。
std::int64_t system_epoch_ms() noexcept {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
}

}  // namespace

etl::expected<std::uint64_t, Error> HostTime::epoch_ms() const noexcept {
  const std::int64_t shifted = system_epoch_ms() + epoch_offset_ms_;
  if (shifted < 0) {
    // 偏移把墙钟拨到 1970 之前：HAL 的 epoch 是无符号 64 位，这种值没有表示法。
    return embark::unexpected(Error::invalid_argument);
  }
  return static_cast<std::uint64_t>(shifted);
}

Error HostTime::set_epoch_ms(std::uint64_t epoch_ms) noexcept {
  epoch_offset_ms_ = static_cast<std::int64_t>(epoch_ms) - system_epoch_ms();
  return Error::none;
}

}  // namespace embark::platform::host
