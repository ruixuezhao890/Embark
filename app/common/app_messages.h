/**
 * Embark 示例 App 的共享消息（App 间只走消息、不互相 include —— spec §7 / issues/08）。
 * id 0x21 是 demo 私有区（框架保留 0xFE 给跨任务信封，勿撞）。
 * 注意：有基类（MessageT）就不是聚合了，必须给显式构造（ETL message 是平凡默认构造）。
 */
#ifndef EMBARK_APP_COMMON_APP_MESSAGES_H
#define EMBARK_APP_COMMON_APP_MESSAGES_H

#include <cstdint>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

/// 亮度百分比消息：SettingsApp 广播，ClockApp 的 onMessage 接收（收到即更新数据；需要
/// “收消息即切状态”的 App 见 docs/guides/state-machine-and-messages.md）。
/// 注意这里只是「告诉别人亮度变了」，真值在 HAL（IDisplay::backlight()）—— 见 ADR 0018。
struct BrightnessMessage : public MessageT<0x21> {
  constexpr BrightnessMessage(std::uint8_t percent_value = 0) noexcept : percent(percent_value) {}
  std::uint8_t percent = 0;  // 0..100 百分比（旧口径 0..3 四档已退役）
};

}  // namespace embark::demo

#endif /* EMBARK_APP_COMMON_APP_MESSAGES_H */
