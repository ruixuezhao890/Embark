// 测试替身：假系统后端。
//
// restart()/fatal() 在真后端不返回，这里必须返回 —— 否则一条断言都写不出来
// （接口故意没给这两个虚拟函数加 [[noreturn]]，见 hal/system.h 的注释）。
#pragma once

#include <embark/hal/system.h>

#include <cstddef>
#include <cstdint>

#include <middleware/etl/string_view.h>

namespace embark::fakes {

class FakeSystem final : public hal::ISystem {
 public:
  FakeSystem() noexcept = default;

  void restart() noexcept override { ++restart_calls; }

  [[nodiscard]] std::size_t free_heap_bytes() const noexcept override { return free_heap; }

  [[nodiscard]] std::size_t min_free_heap_bytes() const noexcept override { return min_free_heap; }

  [[nodiscard]] std::size_t stack_high_water_bytes() const noexcept override {
    return stack_high_water;
  }

  void feed_watchdog() noexcept override { ++watchdog_feeds; }

  void fatal(const char* reason) noexcept override {
    ++fatal_calls;
    std::size_t i = 0;
    if (reason != nullptr) {
      for (; i + 1 < fatal_text.size() && reason[i] != '\0'; ++i) {
        fatal_text[i] = reason[i];
      }
    }
    fatal_length = i;
    fatal_text[i] = '\0';
  }

  [[nodiscard]] etl::string_view last_fatal() const noexcept {
    return etl::string_view(fatal_text.data(), fatal_length);
  }

  // 旋钮与观测点。
  std::size_t free_heap = 0;
  std::size_t min_free_heap = 0;
  std::size_t stack_high_water = 0;
  std::size_t restart_calls = 0;
  std::size_t fatal_calls = 0;
  std::size_t watchdog_feeds = 0;
  etl::array<char, 128> fatal_text{};
  std::size_t fatal_length = 0;
};

}  // namespace embark::fakes
