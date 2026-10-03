// HAL 输入能力：成功路径 + 失败路径（未初始化、队列满、注入的读故障）。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

#include <embark_limits.h>

using embark::Error;
using embark::fake_input_queue_depth;
using embark::fakes::FakeHal;
using embark::fakes::FakeInput;

TEST_CASE("IInput：未 init 就 poll 返回 not_ready") {
  FakeInput input;
  embark::hal::InputEvent event{};

  const auto polled = input.poll(event);

  REQUIRE_FALSE(polled.has_value());
  CHECK(polled.error() == Error::not_ready);
  CHECK(input.poll_calls == 0U);
}

TEST_CASE("IInput：脚本化事件按序取出，取空返回 false") {
  FakeHal hal;
  REQUIRE(hal.init() == Error::none);

  CHECK(hal.input.push(embark::hal::InputEvent{embark::hal::InputEventKind::press, 10, 20, 0, 100}));
  CHECK(hal.input.push(
          embark::hal::InputEvent{embark::hal::InputEventKind::release, 10, 20, 0, 130}));
  CHECK(hal.input.pending() == 2U);

  embark::hal::InputEvent event{};
  const auto first = hal.input.poll(event);
  REQUIRE(first.has_value());
  CHECK(first.value());
  CHECK(event.kind == embark::hal::InputEventKind::press);
  CHECK(event.x == 10U);
  CHECK(event.y == 20U);
  CHECK(event.timestamp_ms == 100U);

  const auto second = hal.input.poll(event);
  REQUIRE(second.has_value());
  CHECK(second.value());
  CHECK(event.kind == embark::hal::InputEventKind::release);
  CHECK(event.timestamp_ms == 130U);

  const auto empty = hal.input.poll(event);
  REQUIRE(empty.has_value());
  CHECK_FALSE(empty.value());
  CHECK(hal.input.pending() == 0U);
}

TEST_CASE("IInput：队列满时丢事件而不是覆盖，丢掉的次数可查") {
  FakeHal hal;
  REQUIRE(hal.init() == Error::none);

  for (std::size_t i = 0; i < fake_input_queue_depth; ++i) {
    CHECK(hal.input.push(embark::hal::InputEvent{}));
  }

  CHECK_FALSE(hal.input.push(embark::hal::InputEvent{}));
  CHECK(hal.input.dropped == 1U);
  CHECK(hal.input.pending() == fake_input_queue_depth);
}

TEST_CASE("IInput：注入读故障走 Error 分支，事件仍在队列里") {
  FakeInput input;
  REQUIRE(input.init() == Error::none);
  REQUIRE(input.push(embark::hal::InputEvent{}));
  input.next_error = Error::io_failure;

  embark::hal::InputEvent event{};
  const auto failed = input.poll(event);

  REQUIRE_FALSE(failed.has_value());
  CHECK(failed.error() == Error::io_failure);
  CHECK(input.pending() == 1U);  // 故障不吞事件：恢复后还能取到
}

TEST_CASE("IInput：init 故障原样透传") {
  FakeInput input;
  input.init_error = Error::corrupt_data;

  CHECK(input.init() == Error::corrupt_data);

  embark::hal::InputEvent event{};
  const auto polled = input.poll(event);
  REQUIRE_FALSE(polled.has_value());
  CHECK(polled.error() == Error::not_ready);
}
