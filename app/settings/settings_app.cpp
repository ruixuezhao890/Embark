/**
 * Embark · 设置 App 实现：见 settings_app.h 头注释。
 *
 * 薄壳纪律与 launcher_app.cpp / clock_app.cpp 完全一致：onCreate 只记账 + 持有框架句柄
 * （本 App 要 publish BrightnessMessage）；onEnter/onResume 只按屏名挂自己的 EEZ 屏；
 * onForegroundTick 泵一帧。手绘按钮/Back to clock 按钮已退役（回主屏由 EEZ 屏按钮 SetPage
 * 回 launcher 屏承担 —— 导航壳 2026-10-06 退役；切 App 也只经 Framework 的 request_switch）。
 */
#include <cstdint>

#include <embark/framework.h>
#include <embark/log.h>

#include "app_messages.h"
#include "settings_app.h"

#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#endif

namespace embark::demo {

void SettingsApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = suspend；界面 = EEZ 屏 settings）", name());
}

void SettingsApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void SettingsApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void SettingsApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void SettingsApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void SettingsApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_tick();
#endif
}

void SettingsApp::bump_level() {
  // 手绘按钮退役后的逻辑入口：档位 0..3 循环 +1，并广播给总线（ClockApp 的
  // onMessage 会收到并更新自己的档位）。谁触发（EEZ Flow 动作/验收程序）不关心。
  level_ = (level_ + 1U) % 4U;  // 四档循环：0..3
  fw_->publish(BrightnessMessage{static_cast<std::uint8_t>(level_)});
  ELOG_INFO("设置页：亮度档 -> {}（已广播 BrightnessMessage）", level_);
}

}  // namespace embark::demo
