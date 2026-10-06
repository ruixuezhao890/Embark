/**
 * Embark · 时钟 App 实现：见 clock_app.h 头注释。
 *
 * 薄壳纪律与 launcher_app.cpp / settings_app.cpp 完全一致（界面提交给 EEZ，手绘
 * lv_scr_load 已退役）：onCreate 只启后台逻辑 + 状态机；onEnter/onResume 只按屏名
 * 挂自己的 EEZ 屏；onForegroundTick 泵一帧并推进显示变量。
 */
#include <cstdint>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "app_messages.h"
#include "clock_app.h"

#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#endif

namespace embark::demo {

// 状态机（示范 §16.4）：led on / led off 两态，tick 事件来回切。
// 转移表（显式 current_state 版）：两个条目分别描述“on 态收 tick → off”与
// “off 态收 tick → on”，action 都是 on_tick（++计数 + 推进 UI 显示变量）；状态的
// on_entry 回调只记日志（画面由 EEZ 屏绑定变量呈现 —— 视觉副作用已退役）。
// 事件不匹配时保持原状态（状态机丢弃该事件）。
const ClockApp::Chart::transition ClockApp::kTransitions[2] = {
    ClockApp::Chart::transition(static_cast<Chart::state_id_t>(State::blinking_on),
                                static_cast<Chart::event_id_t>(Event::tick),
                                static_cast<Chart::state_id_t>(State::blinking_off),
                                &ClockApp::on_tick),
    ClockApp::Chart::transition(static_cast<Chart::state_id_t>(State::blinking_off),
                                static_cast<Chart::event_id_t>(Event::tick),
                                static_cast<Chart::state_id_t>(State::blinking_on),
                                &ClockApp::on_tick),
};

const ClockApp::Chart::state ClockApp::kStates[2] = {
    ClockApp::Chart::state(static_cast<Chart::state_id_t>(State::blinking_on),
                           &ClockApp::enter_blinking_on),
    ClockApp::Chart::state(static_cast<Chart::state_id_t>(State::blinking_off),
                           &ClockApp::enter_blinking_off),
};

ClockApp::ClockApp() noexcept
    : chart_(*this, kTransitions, kTransitions + 2, kStates, kStates + 2,
             static_cast<Chart::state_id_t>(State::blinking_on)) {}

void ClockApp::onCreate(Framework& fw) {
  (void)fw;  // 薄壳不再持有框架句柄（本 App 不 request_switch、不 publish）
  ELOG_INFO("App {} onCreate（后台策略 = tick 100 ms；界面 = EEZ 屏 clock）", name());

  // 状态机就位：触发初始态的 on_entry（记日志），此后 tick 事件驱动往返。
  chart_.start();
}

void ClockApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void ClockApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void ClockApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void ClockApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void ClockApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++foreground_ticks_;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  // 泵一帧：ui_tick() = lv_timer_handler + tick_screen(当前屏)—— 绑了 Flow 全局
  // 变量的控件每帧在这里读到最新值（变量桥的路由，见 eez_ui_bridge.h「四」）。
  eez_ui_bridge_tick();
#endif
}

void ClockApp::onBackgroundTick(std::uint32_t now_ms) {
  // 状态机吃一个 tick 事件：led on/off 翻转 + 拍数 +1。事件没有参数（TParameter=void）。
  chart_.process_event(static_cast<Chart::event_id_t>(Event::tick));
  if (ticks_ % 10 == 0) {
    ELOG_INFO("clock 后台节拍：第 {} 拍（约 {} s，now={} ms）", ticks_, ticks_ / 10, now_ms);
  }
}

void ClockApp::onMessage(const Message& msg) {
  if (msg.get_message_id() == BrightnessMessage::ID) {
    // 消息驱动不进状态机：亮度档是“值”不是“行为”，直接更新数据（界面显示交给
    // EEZ 屏绑定的变量，C++ 不再碰控件 —— 示例说明：不是所有输入都要过状态机，
    // 状态机表达周期性/时序行为）。
    const auto& brightness = static_cast<const BrightnessMessage&>(msg);
    level_ = brightness.level;
    ELOG_INFO("clock 收到 BrightnessMessage：亮度档 -> {}", level_);
  }
}

void ClockApp::on_tick() noexcept {
  ++ticks_;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  // 值变化时推进 UI 显示变量（EEZ Studio 里声明 clock_tick_count 并绑到控件即自动
  // 生效；还没声明时 set_var_int 返回 false、无副作用 —— 见 eez_ui_bridge.h「四」）。
  eez_ui_bridge_set_var_int("clock_tick_count", static_cast<std::int32_t>(ticks_));
#endif
}

void ClockApp::enter_blinking_on() noexcept {
  // 视觉副作用已退役：led 亮的呈现改由 EEZ 屏绑定变量按值显示，这里只记日志。
  ELOG_INFO("clock 状态机 -> led on");
}

void ClockApp::enter_blinking_off() noexcept {
  ELOG_INFO("clock 状态机 -> led off");
}

}  // namespace embark::demo
