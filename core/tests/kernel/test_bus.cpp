/**
 * 内核测试：总线（issues/07）
 *
 * 验证 Bus 的订阅/退订/广播语义与"无人接收"的 WARN+计数（spec §7）。
 * 测试不装 logger（elog 无默认 logger 时 log_at 是 no-op），计数即可测。
 */
#include <cstdint>

#include <doctest/doctest.h>

#include <embark/bus.h>
#include <embark/message.h>

namespace {

/// 测试路由：按构造参数决定接受哪些消息 id。
class FakeRouter final : public etl::imessage_router {
 public:
  explicit FakeRouter(const char* name, etl::message_id_t accepted_id)
      : etl::imessage_router(++next_router_id), name_(name), accepted_id_(accepted_id) {}

  void receive(const etl::imessage&) override { ++delivered_; }

  bool accepts(etl::message_id_t id) const override { return id == accepted_id_; }

  [[nodiscard]] bool is_null_router() const override { return false; }
  [[nodiscard]] bool is_producer() const override { return false; }
  [[nodiscard]] bool is_consumer() const override { return true; }

  [[nodiscard]] const char* name() const { return name_; }
  [[nodiscard]] std::uint32_t delivered() const { return delivered_; }

 private:
  static inline etl::message_router_id_t next_router_id = 0;
  const char* name_ = nullptr;
  etl::message_id_t accepted_id_ = 0;
  std::uint32_t delivered_ = 0;
};

/// 测试消息：固定载荷，方便核对投递内容。
using TestMessageA = embark::MessageT<0x11>;
using TestMessageB = embark::MessageT<0x22>;

}  // namespace

TEST_CASE("总线：订阅者能收到广播，不接受的 ids 被过滤") {
  embark::Bus bus;
  FakeRouter a{"a", 0x11};
  FakeRouter b{"b", 0x22};

  REQUIRE(bus.subscribe(a));
  REQUIRE(bus.subscribe(b));
  CHECK(bus.subscriber_count() == 2);

  bus.publish(TestMessageA{});
  CHECK(a.delivered() == 1);
  CHECK(b.delivered() == 0);  // b 只接受 0x22

  bus.publish(TestMessageB{});
  CHECK(a.delivered() == 1);
  CHECK(b.delivered() == 1);

  CHECK(bus.published() == 2);
  CHECK(bus.unknown() == 0);
}

TEST_CASE("总线：无人订阅的消息计 unknown 且不派发") {
  embark::Bus bus;
  FakeRouter a{"a", 0x22};  // 只接受 0x22

  REQUIRE(bus.subscribe(a));
  bus.publish(TestMessageA{});  // 0x11 无人要
  CHECK(a.delivered() == 0);
  CHECK(bus.published() == 1);
  CHECK(bus.unknown() == 1);

  bus.reset_counters();
  CHECK(bus.published() == 0);
  CHECK(bus.unknown() == 0);
}

TEST_CASE("总线：退订后不再接收；重复订阅被拒") {
  embark::Bus bus;
  FakeRouter a{"a", 0x11};

  REQUIRE(bus.subscribe(a));
  CHECK_FALSE(bus.subscribe(a));  // 同一路由重复订阅：基类拒绝

  bus.unsubscribe(a);
  CHECK(bus.subscriber_count() == 0);

  bus.publish(TestMessageA{});
  CHECK(a.delivered() == 0);
  CHECK(bus.unknown() == 1);
}

TEST_CASE("总线：ALL 退订清空所有订阅者") {
  embark::Bus bus;
  FakeRouter a{"a", 0x11};
  FakeRouter b{"b", 0x22};
  REQUIRE(bus.subscribe(a));
  REQUIRE(bus.subscribe(b));

  bus.unsubscribe(etl::imessage_router::ALL_MESSAGE_ROUTERS);
  CHECK(bus.subscriber_count() == 0);
  bus.publish(TestMessageA{});
  bus.publish(TestMessageB{});
  CHECK(a.delivered() == 0);
  CHECK(b.delivered() == 0);
}