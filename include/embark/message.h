/**
 * Embark · 框架消息（issues/06）
 *
 * spec §7 的结论：消息直接复用 ETL 现成设施（etl::message / etl::imessage），
 * 这里只做两个别名，让 App 代码少敲一个 namespace。类型化消息与订阅/派发
 * （issue 07）全部在 ETL 之上；本头文件只是"内核允许 include 的最小消息面"。
 */
#ifndef EMBARK_MESSAGE_H
#define EMBARK_MESSAGE_H

#include <middleware/etl/message.h>

namespace embark {

/// 所有框架消息的基类（等义 etl::imessage：虚表 + get_message_id()）。
using Message = etl::imessage;

/// 消息 id 的类型（等义 etl::message_id_t）。
using MessageId = etl::message_id_t;

/// 具体消息：`class MyMessage : public embark::MessageT<42> { ... };`
template <MessageId Id>
using MessageT = etl::message<Id>;

}  // namespace embark

#endif /* EMBARK_MESSAGE_H */