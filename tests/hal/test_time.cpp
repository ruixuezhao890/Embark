// HAL 时间能力：成功路径 + 失败路径。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

using embark::Error;
using embark::fakes::FakeHal;
using embark::fakes::FakeTime;

TEST_CASE("ITime：delay_ms 推进虚拟时钟（不真睡）") {
  FakeTime time;
  CHECK(time.now_ms() == 0U);

  time.delay_ms(250U);

  CHECK(time.now_ms() == 250U);
  CHECK(time.delay_calls == 1U);
  CHECK(time.last_delay_ms == 250U);
}

TEST_CASE("ITime：epoch_ms 正常返回 64 位毫秒") {
  FakeTime time;
  const auto epoch = time.epoch_ms();

  REQUIRE(epoch.has_value());
  CHECK(epoch.value() == 1700000000000ULL);
}

TEST_CASE("ITime：epoch_ms 故障返回 Error 码") {
  FakeTime time;
  time.fail_epoch_ms(Error::unsupported);

  const auto epoch = time.epoch_ms();

  REQUIRE_FALSE(epoch.has_value());
  CHECK(epoch.error() == Error::unsupported);
}

TEST_CASE("ITime：set_epoch_ms 之后 epoch_ms 立刻是新值") {
  FakeTime time;
  const std::uint64_t target = 1792195200000ULL;  // 2026-10-17T00:00:00Z

  CHECK(time.set_epoch_ms(target) == Error::none);

  const auto epoch = time.epoch_ms();
  REQUIRE(epoch.has_value());
  // 允许一次 tick 的推进（宿主/真机都不是"写完全等于读"）。
  CHECK(epoch.value() >= target);
  CHECK(epoch.value() <= target + 2U);
  CHECK(time.epoch_write_calls == 1U);
}

TEST_CASE("ITime：没有 RTC 的装配设表失败且不改变墙钟") {
  FakeTime time;
  time.fail_set_epoch_ms(Error::unsupported);
  const std::uint64_t before = time.epoch_value;

  CHECK(time.set_epoch_ms(before + 60000ULL) == Error::unsupported);

  const auto epoch = time.epoch_ms();
  REQUIRE(epoch.has_value());
  CHECK(epoch.value() == before);
}

TEST_CASE("Context：经基类引用拿到的是同一份时钟") {
  FakeHal hal;
  embark::hal::Context ctx = hal.context();

  ctx.time.delay_ms(7U);

  CHECK(hal.time.now_ms() == 7U);
  CHECK(ctx.time.now_ms() == 7U);
}
