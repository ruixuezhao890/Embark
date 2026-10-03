// 测试替身：可控时钟。
//
// 公开成员是给测试用的旋钮，测试直接改；delay 不会真的睡，只推进虚拟时钟，
// 因此用到时间的测试既确定又瞬时。
#pragma once

#include <embark/hal/time.h>

#include <cstddef>
#include <cstdint>

namespace embark::fakes {

class FakeTime final : public hal::ITime {
 public:
  FakeTime() noexcept = default;

  std::uint32_t now_ms() const noexcept override { return clock_ms; }

  void delay_ms(std::uint32_t milliseconds) noexcept override {
    last_delay_ms = milliseconds;
    ++delay_calls;
    clock_ms += milliseconds;
  }

  etl::expected<std::uint64_t, Error> epoch_ms() const noexcept override {
    if (epoch_error != Error::none) {
      return unexpected(epoch_error);
    }
    return epoch_value;
  }

  void set_now_ms(std::uint32_t value) noexcept { clock_ms = value; }
  void set_epoch_ms(std::uint64_t value) noexcept { epoch_value = value; }
  void fail_epoch_ms(Error error) noexcept { epoch_error = error; }

  // 旋钮与观测点。
  std::uint32_t clock_ms = 0;
  std::uint64_t epoch_value = 1700000000000ULL;  // 2023-11-14T22:13:20Z
  Error epoch_error = Error::none;
  std::uint32_t last_delay_ms = 0;
  std::size_t delay_calls = 0;
};

}  // namespace embark::fakes
