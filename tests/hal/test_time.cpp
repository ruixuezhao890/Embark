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

TEST_CASE("Context：经基类引用拿到的是同一份时钟") {
  FakeHal hal;
  embark::hal::Context ctx = hal.context();

  ctx.time.delay_ms(7U);

  CHECK(hal.time.now_ms() == 7U);
  CHECK(ctx.time.now_ms() == 7U);
}
