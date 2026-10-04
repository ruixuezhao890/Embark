/**
 * Embark · 应用总线（issues/07）
 *
 * spec §7 的消息投递：基于 etl::message_bus<max_bus_subscribers>，补上 ETL
 * 没有的两件事：
 *   - publish()：ETL 的 bus 只给 subscribe/unsubscribe/receive（receive 还是
 *     虚函数，由消息源自己调），没有一个面向调用方的"发消息"入口；
 *   - 无人接收的可见性：ETL 的广播对"没有任何订阅者 accepts 这个消息 id"
 *     静默处理（message_bus.h:142 起，遍历不到就什么都不发生）。框架要求
 *     这种编程错误（发的消息没人听）必须 WARN + 计数（issue 07 工单）。
 *
 * 实现要点：基类的 router_list 是 private，bus 自己维护一份同容量的镜像表
 * （只存 subscribe 成功的消费者），publish 先查镜像判断"有没有接收者"。
 */
#ifndef EMBARK_BUS_H
#define EMBARK_BUS_H

#include <cstddef>
#include <cstdint>

#include <middleware/etl/message_bus.h>
#include <middleware/etl/vector.h>

#include <embark/message.h>
#include <embark_limits.h>

namespace embark {

/// 应用总线。只被唯一 UI 任务（与后台 tick 同线程）使用，总线本身无锁。
class Bus final : public etl::message_bus<max_bus_subscribers> {
 public:
  Bus() noexcept = default;

  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;

  /// 订阅（包装基类：is_consumer() 检查 + 上限断言都来自 ETL，成功同步镜像）。
  /// 返回 false = 该 router 不消费消息（或表已满，属编程错误，基类会断言）。
  [[nodiscard]] bool subscribe(etl::imessage_router& router) noexcept;

  /// 退订（按 router 引用）。
  void unsubscribe(etl::imessage_router& router) noexcept;

  /// 退订（按 router id；ALL 语义同基类 = 清空）。
  void unsubscribe(etl::message_router_id_t id) noexcept;

  /// 清空所有订阅。
  void clear_subscribers() noexcept;

  /// 发消息：全广播（与基类 receive(ALL, message) 同样的派发语义）。
  /// 没有任何订阅者 accepts 该消息 id → WARN + 计数，不发（编程错误可见）。
  void publish(const etl::imessage& message) noexcept;

  // --- 观测 ---------------------------------------------------------------

  /// 发过的消息总数（含无人接收丢弃的）。
  [[nodiscard]] std::uint32_t published() const noexcept { return published_; }

  /// 无人接收而被丢弃的消息数（编程错误的信号）。
  [[nodiscard]] std::uint32_t unknown() const noexcept { return unknown_; }

  /// 当前订阅者数量（镜像表大小）。
  [[nodiscard]] std::size_t subscriber_count() const noexcept { return subscribers_.size(); }

  /// 清零计数（观测/日志打点用）。
  void reset_counters() noexcept {
    published_ = 0;
    unknown_ = 0;
  }

 private:
  etl::vector<etl::imessage_router*, max_bus_subscribers> subscribers_;
  std::uint32_t published_ = 0;
  std::uint32_t unknown_ = 0;
};

}  // namespace embark

#endif /* EMBARK_BUS_H */