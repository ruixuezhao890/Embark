// HAL 系统能力：宿主机量不到的水位返回 0、restart/fatal 由后端实现上报，
// 以及"经 Context 聚合起来的那一份"就是调用方拿到的那一份。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

using embark::fakes::FakeHal;
using embark::fakes::FakeSystem;

TEST_CASE("ISystem：宿主机量不到的水位返回 0（0 = 该后端量不了）") {
  FakeSystem system;

  CHECK(system.free_heap_bytes() == 0U);
  CHECK(system.min_free_heap_bytes() == 0U);
  CHECK(system.stack_high_water_bytes() == 0U);
}

TEST_CASE("ISystem：feed_watchdog 与 restart 各自计数") {
  FakeSystem system;

  system.feed_watchdog();
  system.feed_watchdog();
  CHECK(system.watchdog_feeds == 2U);
  CHECK(system.restart_calls == 0U);

  system.restart();
  CHECK(system.restart_calls == 1U);
}

TEST_CASE("ISystem：fatal 记录原因后返回（真后端不返回，替身必须能断言）") {
  FakeSystem system;
  CHECK(system.fatal_calls == 0U);

  system.fatal("hal: 无可用显示后端");

  CHECK(system.fatal_calls == 1U);
  CHECK(system.last_fatal() == etl::string_view("hal: 无可用显示后端"));
  CHECK(system.watchdog_feeds == 0U);
}

TEST_CASE("Context：系统能力经聚合结构原样暴露") {
  FakeHal hal;
  hal.system.free_heap = 4096U;
  hal.system.stack_high_water = 512U;

  embark::hal::Context ctx = hal.context();

  ctx.system.feed_watchdog();
  CHECK(hal.system.watchdog_feeds == 1U);
  CHECK(ctx.system.free_heap_bytes() == 4096U);
  CHECK(ctx.system.stack_high_water_bytes() == 512U);
}

TEST_CASE("测试侧诊断钩子：没有用例真的踩响断言，就应当是 0") {
  CHECK(embark::fakes::fatal_calls() == 0U);
  CHECK(embark::fakes::assert_failed_calls() == 0U);
}
