// 测试替身：假输入后端。
//
// 事件从一条定长环形队列里手工推入（脚本化输入），poll() 按序吐出。
#pragma once

#include <embark/hal/input.h>
#include <embark_limits.h>

#include <cstddef>

namespace embark::fakes {

class FakeInput final : public hal::IInput {
 public:
  FakeInput() noexcept = default;

  Error init() noexcept override {
    ++init_calls;
    if (init_error != Error::none) {
      return init_error;
    }
    ready = true;
    return Error::none;
  }

  [[nodiscard]] etl::expected<bool, Error> poll(hal::InputEvent& event) noexcept override {
    if (!ready) {
      return unexpected(Error::not_ready);
    }
    ++poll_calls;
    if (next_error != Error::none) {
      return unexpected(next_error);
    }
    if (count == 0) {
      return false;
    }
    event = queue[head];
    head = (head + 1) % queue.size();
    --count;
    return true;
  }

  /// 给测试用的脚本入口：队列满返回 false（模拟"事件被丢掉"）。
  bool push(const hal::InputEvent& event) noexcept {
    if (count == queue.size()) {
      ++dropped;
      return false;
    }
    queue[tail] = event;
    tail = (tail + 1) % queue.size();
    ++count;
    return true;
  }

  void clear() noexcept {
    head = 0;
    tail = 0;
    count = 0;
  }

  [[nodiscard]] std::size_t pending() const noexcept { return count; }

  // 旋钮与观测点。
  Error init_error = Error::none;
  Error next_error = Error::none;
  bool ready = false;
  std::size_t init_calls = 0;
  std::size_t poll_calls = 0;
  std::size_t dropped = 0;
  etl::array<hal::InputEvent, fake_input_queue_depth> queue{};
  std::size_t head = 0;
  std::size_t tail = 0;
  std::size_t count = 0;
};

}  // namespace embark::fakes
