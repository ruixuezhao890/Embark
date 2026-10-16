/**
 * Embark · 设置 App 实现：见 settings_app.h 头注释。
 *
 * 薄壳纪律与 launcher_app.cpp / clock_app.cpp 完全一致：onCreate 只记账 + 持有框架句柄
 * （本 App 要 publish BrightnessMessage）；onEnter/onResume 只按屏名挂自己的屏（后端由
 * app/common/ui_backend.h 选：AiDoGUI 优先，EEZ 并存）；onForegroundTick 泵一帧。
 * 手绘按钮/Back to clock 按钮已退役（回主屏由屏上的 Btn_Back 承担 —— 设计器「调用函数」
 * embark_ui_open_launcher；切 App 也只经 Framework 的 request_switch）。屏上「亮度 +1」不是导航：
 * onCreate 里用 ui_backend_bind_action("settings", "Btn_Bright", ...) 挂成本 App 的逻辑动作，按钮
 * 随屏销毁消失、回调由导航桥在每次装载后重挂，所以这里不自己 add_event_cb（ADR 0014 决策 2）。
 */
#include <cstdint>

#include <embark/framework.h>
#include <embark/log.h>

#include "app_messages.h"
#include "settings_app.h"

#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
#include "ui_backend.h"
#endif

namespace embark::demo {
namespace {

/// 屏上「亮度 +1」按钮的落点。回调是裸函数指针（不能捕获 this），所以用一个静态指针找回
/// **当前**实例：同一时刻只有前台的 SettingsApp 会装自己的屏。
SettingsApp* g_settings_for_ui = nullptr;

void on_screen_bump_level() {
  if (g_settings_for_ui != nullptr) {
    g_settings_for_ui->bump_level();
  }
}

}  // namespace

void SettingsApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = suspend；界面 = 屏 settings）", name());
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  g_settings_for_ui = this;
  // 「亮度 +1」不是导航，是本 App 的逻辑动作：绑在屏上的按钮上，由导航桥在每次装载后重挂
  // （按钮随屏 destroy 一起消失，所以不能在这里自己 add_event_cb）。返回按钮走设计器里的
  // 「调用函数」事件 embark_ui_open_launcher，与 clock/weather/music/album 四屏一致。
  ui_backend_bind_action("settings", "Btn_Bright", &on_screen_bump_level);
#endif
}

void SettingsApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());
#endif
}

void SettingsApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void SettingsApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());
#endif
}

void SettingsApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void SettingsApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_tick();
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
