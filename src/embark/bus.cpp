/**
 * Embark · 应用总线实现（issues/07）
 */
#include <embark/bus.h>

#include <embark/log.h>

namespace embark {

bool Bus::subscribe(etl::imessage_router& router) noexcept {
  // ETL 的 message_bus 按 id 排序插入、不查重（重复订阅会重复插），
  // 这里用镜像表做一次显式查重：同一路由重复订阅 = 拒绝。
  for (const etl::imessage_router* const existing : subscribers_) {
    if (existing == &router) {
      return false;
    }
  }
  const bool ok = etl::message_bus<max_bus_subscribers>::subscribe(router);
  if (ok) {
    subscribers_.push_back(&router);  // 与基类同容量，满了基类已断言，这里不会失败
  }
  return ok;
}

void Bus::unsubscribe(etl::imessage_router& router) noexcept {
  etl::message_bus<max_bus_subscribers>::unsubscribe(router);
  for (auto it = subscribers_.begin(); it != subscribers_.end(); ++it) {
    if (*it == &router) {
      subscribers_.erase(it);
      break;
    }
  }
}

void Bus::unsubscribe(etl::message_router_id_t id) noexcept {
  etl::message_bus<max_bus_subscribers>::unsubscribe(id);
  if (id == etl::imessage_router::ALL_MESSAGE_ROUTERS) {
    subscribers_.clear();
    return;
  }
  for (auto it = subscribers_.begin(); it != subscribers_.end();) {
    if ((*it)->get_message_router_id() == id) {
      it = subscribers_.erase(it);
    } else {
      ++it;
    }
  }
}

void Bus::clear_subscribers() noexcept {
  etl::message_bus<max_bus_subscribers>::unsubscribe(
      etl::imessage_router::ALL_MESSAGE_ROUTERS);
  subscribers_.clear();
}

void Bus::publish(const etl::imessage& message) noexcept {
  ++published_;

  // 有没有人愿意听？镜像表与基类同步，这里只做只读判断，不发消息。
  bool has_receiver = false;
  for (etl::imessage_router* const router : subscribers_) {
    if (router->accepts(message.get_message_id())) {
      has_receiver = true;
      break;
    }
  }
  if (!has_receiver) {
    ++unknown_;
    ELOG_WARN("总线：消息 id {} 无人订阅，已丢弃（累计 {} 次）",
              static_cast<unsigned>(message.get_message_id()), unknown_);
    return;
  }

  // 广播（基类语义：遍历订阅表，accepts 才 receive）。
  receive(message);
}

}  // namespace embark