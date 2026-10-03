// 一套假后端 + 由它们搭出的 HAL 上下文。
//
// 装配方式刻意与宿主一致（Context 聚合 + 每平台一份实例），所以测试里跑的调用链
// 和真机/宿主上完全同形；测试自己持有 FakeHal，不依赖 platform_context()，
// 因此测试 target 不需要（也不该）链接平台库。
#pragma once

#include "fake_bus.h"
#include "fake_display.h"
#include "fake_input.h"
#include "fake_log_sink.h"
#include "fake_persistence.h"
#include "fake_system.h"
#include "fake_time.h"

#include <cstddef>

#include <embark/hal/context.h>
#include <middleware/etl/string_view.h>

namespace embark::fakes {

struct FakeHal {
  FakeTime time;
  FakeDisplay display;
  FakeInput input;
  FakePersistence storage;
  FakeLogSink log;
  FakeSystem system;
  FakeBus bus;

  [[nodiscard]] hal::Context context() noexcept {
    return hal::Context{time, storage, log, system, bus, &display, &input};
  }

  /// 把需要显式打开的后端都开一遍（时间与总线没有 init）。
  [[nodiscard]] Error init() noexcept {
    const Error results[] = {display.init(), input.init(), storage.init()};
    for (const Error error : results) {
      if (error != Error::none) {
        return error;
      }
    }
    log.init();
    return Error::none;
  }
};

// 测试侧的诊断钩子观测点（定义在 fakes.cpp）。
// 说明：assert_failed/fatal 记录之后照样 abort —— 它们能写进这里的次数只有 0，
// 非 0 就意味着某个用例真的把断言/致命路径踩响了。
[[nodiscard]] std::size_t assert_failed_calls() noexcept;
[[nodiscard]] std::size_t fatal_calls() noexcept;
[[nodiscard]] etl::string_view last_failure_text() noexcept;
void reset_diagnostics() noexcept;

}  // namespace embark::fakes
