/**
 * Embark 固定容量上限（spec §10）
 *
 * 内核零堆：所有容量都是编译期常量，都能在代码里查到，也都必须在这里集中列出。
 * 这些是 v1 的初值，落地 App/消息/定时器时按实测调整（issues/06、07）。
 */
#ifndef EMBARK_LIMITS_H
#define EMBARK_LIMITS_H

#include <cstddef>

namespace embark {

// App 总数（静态注册表；注册顺序 = 默认前台顺序）
inline constexpr std::size_t kMaxApps = 8;

// 框架定时器数量（后台节拍用 etl::callback_timer<kMaxBackgroundTimers>）
inline constexpr std::size_t kMaxBackgroundTimers = 8;

// 一条消息队列的深度（跨任务单生产者单消费者）
inline constexpr std::size_t kMessageQueueDepth = 16;

}  // namespace embark

#endif /* EMBARK_LIMITS_H */
