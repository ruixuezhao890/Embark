// 假姿态后端：只为实现 hal::IImu 的三条契约（issue 08）。
//
// 刻意不塞进 FakesHal —— 既有用例断言「三个服务槽都是 nullptr」，把 IMU 塞进去会让那条
// 断言变味（它防的是「有人以为加了服务就能随便接线」）。要测契约的用例自己持有一个 FakeImu，
// 需要时再自己拼一个 Context 把它接上去。
#pragma once

#include <embark/error.h>
#include <embark/hal/imu.h>

namespace embark::fakes {

class FakeImu final : public hal::IImu {
 public:
  [[nodiscard]] Error init() noexcept override {
    ++init_calls;
    if (init_result != Error::none) {
      return init_result;
    }
    ready_ = true;
    return Error::none;
  }

  [[nodiscard]] Error read(hal::ImuSample& out) noexcept override {
    ++read_calls;
    if (!ready_) {
      return Error::not_ready;
    }
    if (read_result != Error::none) {
      return read_result;  // 注入总线失败：调用方必须能看见 io_failure 而不是旧值
    }
    out = sample;
    return Error::none;
  }

  void set_sample(const hal::ImuSample& value) noexcept { sample = value; }

  [[nodiscard]] bool ready() const noexcept { return ready_; }

  hal::ImuSample sample{};          // read 成功时写进 out 的那一份
  Error init_result = Error::none;  // 注入 init 失败（not_found / io_failure）
  Error read_result = Error::none;  // 注入 read 失败
  int init_calls = 0;               // 观测调用次数：init 幂等靠它验
  int read_calls = 0;

 private:
  bool ready_ = false;
};

}  // namespace embark::fakes
