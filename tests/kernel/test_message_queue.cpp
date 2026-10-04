/**
 * 内核测试：跨任务消息队列（issues/07）
 *
 * 验证 MessageQueue 的 FIFO、满时覆盖最旧 + 溢出计数、空读失败、
 * 以及默认 etl::mutex 版在单线程下的可用性。
 */
#include <cstdint>

#include <doctest/doctest.h>

#include <embark/message_queue.h>

TEST_CASE("消息队列：FIFO 顺序与空满状态") {
  embark::MessageQueue<std::uint32_t, 4> queue;

  CHECK(queue.empty());
  CHECK_FALSE(queue.full());

  queue.push(10);
  queue.push(20);
  queue.push(30);
  CHECK_FALSE(queue.empty());
  CHECK(queue.size() == 3);

  std::uint32_t out = 0;
  REQUIRE(queue.pop(out));
  CHECK(out == 10);
  REQUIRE(queue.pop(out));
  CHECK(out == 20);
  REQUIRE(queue.pop(out));
  CHECK(out == 30);

  CHECK(queue.empty());
  CHECK_FALSE(queue.pop(out));  // 空读失败
}

TEST_CASE("消息队列：满时覆盖最旧并计溢出") {
  embark::MessageQueue<std::uint32_t, 3> queue;

  queue.push(1);
  queue.push(2);
  queue.push(3);
  CHECK(queue.full());

  queue.push(4);  // 溢出：最旧的 1 被覆盖
  queue.push(5);  // 溢出：2 被覆盖
  CHECK(queue.overflows() == 2);

  std::uint32_t out = 0;
  REQUIRE(queue.pop(out));
  CHECK(out == 3);  // 幸存 3、4、5，FIFO 相对顺序保持
  REQUIRE(queue.pop(out));
  CHECK(out == 4);
  REQUIRE(queue.pop(out));
  CHECK(out == 5);

  queue.reset_overflows();
  CHECK(queue.overflows() == 0);
}

TEST_CASE("消息队列：移动与拷贝入队均可用") {
  embark::MessageQueue<std::uint32_t, 4> queue;

  queue.push(std::uint32_t{42});  // 右值 → T&& 版
  std::uint32_t v = 43;
  queue.push(v);                  // 左值 → const T& 版

  std::uint32_t out = 0;
  REQUIRE(queue.pop(out));
  CHECK(out == 42);
  REQUIRE(queue.pop(out));
  CHECK(out == 43);
}

TEST_CASE("消息队列：默认 etl::mutex 版可实例化（FreeRTOS 静态互斥锁）") {
  // 单线程下 take/give 走快路径；此用例主要验证 dtors/符号链接没有问题。
  embark::MessageQueue<std::uint32_t, 2> queue;
  queue.push(7);
  std::uint32_t out = 0;
  REQUIRE(queue.pop(out));
  CHECK(out == 7);
}