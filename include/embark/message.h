/**
 * Embark · 框架消息（issues/06、07）
 *
 * spec §7 的结论：消息直接复用 ETL 现成设施（etl::message / etl::imessage），
 * 这里做别名并补充两个框架自留件：
 *   - Message / MessageId / MessageT：App 代码少敲一层 namespace；
 *   - AppId、invalid_app_id：App 编号原本是"注册表下标"概念，但消息投递、
 *     跨任务信封都引用它，所以定义放这里（app.h 通过本头文件获得）；
 *   - cross_task_message_id + CrossTaskMessage：own task → UI 的统一信封
 *     （issue 07）。任何跨任务消息都装进这个信封走 Framework::post()，
 *     UI 循环在 step 的派发段把它经总线投递给目标 App 的 onMessage。
 */
#ifndef EMBARK_MESSAGE_H
#define EMBARK_MESSAGE_H

#include <cstdint>

#include <middleware/etl/message.h>

namespace embark {

/// 所有框架消息的基类（等义 etl::imessage：虚表 + get_message_id()）。
using Message = etl::imessage;

/// 消息 id 的类型（等义 etl::message_id_t）。
using MessageId = etl::message_id_t;

/// 具体消息：`class MyMessage : public embark::MessageT<42> { ... };`
template <MessageId Id>
using MessageT = etl::message<Id>;

/// App 的编号：注册表下标（0..size-1）。消息投递、request_switch 都用它。
using AppId = std::uint8_t;

/// 无效 App 编号（find 失败时返回）。
inline constexpr AppId invalid_app_id = 0xFFU;

/// 框架保留消息 id：跨任务信封（见文件头注释）。
inline constexpr MessageId cross_task_message_id = 0xFEU;

/// 跨任务消息信封：own task 把"要给 UI 的话"装进信封投递（值语义，可整体入队）。
/// 接收方在 onMessage 里按 get_message_id() == cross_task_message_id 认出它。
class CrossTaskMessage : public MessageT<cross_task_message_id> {
 public:
  CrossTaskMessage() noexcept = default;
  CrossTaskMessage(AppId from, std::uint32_t sequence) noexcept
      : from_app(from), seq(sequence) {}

  AppId from_app = invalid_app_id;  ///< 发送方 App（own task 的拥有者）
  std::uint32_t seq = 0;            ///< 发送方自己的序号（丢包/乱序观测）
};

}  // namespace embark

#endif /* EMBARK_MESSAGE_H */