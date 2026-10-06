/**
 * Embark 示例 App 实现（issues/08；issue 16 起主屏是启动器；issue 19 起界面由
 * EEZ Studio 生成代码提供 —— 手绘 UI 已全部退役，本文件只留 App 逻辑的薄壳）
 *
 * 薄壳纪律（与 launcher_app.cpp / eez_demo_app.cpp 同形，详见 demo_apps.h 头注释）：
 *   - onCreate 不再建屏：只记账/启后台逻辑（ClockApp 顺带启动状态机 chart_.start()）；
 *   - onEnter/onResume 只做一件事 —— 按屏名约定加载自己的 EEZ 屏
 *       （eez_ui_bridge_enter_app_screen：Studio 还没画同名屏时保持当前屏 + 告警）；
 *   - onForegroundTick 泵 eez_ui_bridge_tick（LVGL 一帧 = tick_screen_* 刷新绑了
 *       Flow 全局变量的控件）；值变化的 App（ClockApp 每拍）把数据显示推进变量；
 *   - 切换仍只经 fw.request_switch()；App 之间仍只走消息（SettingsApp publish →
 *       ClockApp onMessage）；绝不自己调 lv_timer_handler()（那是 UI 端口的活，
 *       现在由前台 App 的 onForegroundTick 泵 —— 同一件事，入口在薄壳侧）。
 *
 * 编译开关 EMBARK_EEZ_UI_BRIDGE（see demo_apps.h）：宿主构建定义它，桥调用生效；
 * 未定义的目标（esp32 真机，其组件清单还没接 eez_ui_* 与生成代码）把桥调用编译成
 * no-op —— App 逻辑照常可跑，真机接入 EEZ 后打开开关即可。
 */
#include <cstdint>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "demo_apps.h"

#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#endif

namespace embark::demo {

// ================================ ClockApp ================================
// 状态机（示范 §16.4）：led on / led off 两态，tick 事件来回切。
// 转移表（显式 current_state 版）：两个条目分别描述"on 态收 tick → off"与
// "off 态收 tick → on"，action 都是 on_tick（++计数 + 推进 UI 显示变量）；状态的
// on_entry 回调只记日志（画面由 EEZ 屏绑定变量呈现 —— 视觉副作用随手绘 UI 退役）。
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
    // 消息驱动不进状态机：亮度档是"值"不是"行为"，直接更新数据（界面显示交给
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

// ================================ SettingsApp ================================
// 本 App 不再有"Back to clock"按钮：回主屏由 EEZ 屏按钮 SetPage 回 launcher 屏承担
// （导航壳已退役）；切 App 也只经 Framework 的 request_switch。
// （issue 19）手绘"Level +1"按钮退役：亮度档逻辑入口 bump_level() 交给 EEZ Flow
// 动作/变量或无人值守验收调用 —— 界面在 Studio 画好 settings 屏后由 EEZ 屏提供。

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

// ================================ TickerApp ================================

void TickerApp::onCreate(Framework& fw) {
  fw_ = &fw;
  // 数值一律取自 settings()：以前这里硬编码过"栈 256 字"，改成 512 后日志会说谎。
  const AppSettings configured = settings();
  ELOG_INFO("App {} onCreate（own_task，周期 {} ms，栈 {} 字，优先级 {}）", name(),
            configured.period_ms, configured.task_stack_words, configured.task_priority);
}

void TickerApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void TickerApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void TickerApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void TickerApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_tick();
#endif
}

void TickerApp::onBackgroundTick(std::uint32_t now_ms) {
  const std::uint32_t sequence = ++sent_;
  const AppId self_id = fw_->id_of(*this);
  const CrossTaskMessage envelope(self_id, sequence);
  // 整对象打（issue 13）：字段名来自 message.h 里的 E_FMT_FIELDS，加字段不用改这行。
  // 栈预算警告（issue 22）：这条日志实测要 ~1.9 KB 栈——AppId 是 std::uint16_t，efmt 没有它的
  // 原生格式化器，走的是 std::ostringstream 兜底。TickerApp::settings() 给的 512 字（4 KB）
  // 是量出来的最低值，别调小（调小 = 踩穿自己的 TCB，core 0 的 SysTick 里 LoadProhibited）。
  ELOG_INFO("ticker 后台任务：{}（now={} ms）", envelope, now_ms);
  // 跨任务投递：post() 只入收件箱（锁在队列内部），UI 循环在派发段广播。
  fw_->post(envelope);
}

void TickerApp::onMessage(const Message& msg) {
  if (msg.get_message_id() != cross_task_message_id) {
    return;
  }
  ++received_;
  const auto& envelope = static_cast<const CrossTaskMessage&>(msg);
  ELOG_INFO("UI 收到 ticker 消息：{}，累计收 {} 条", envelope, received_);
}

void TickerApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

// ================================ JobApp ===================================
// 一次性任务：period_ms = 0 由框架解释成"跑一轮就结束"（run_own_task 的零周期分支）。
// 任务体里没有循环、没有 delay —— 返回即结束，回收由框架的 step 负责。

void JobApp::onCreate(Framework& fw) {
  (void)fw;  // 本 App 不投消息、不请求切换：任务体只做一轮记账
  ELOG_INFO("App {} onCreate（own_task，period_ms = 0：跑完即结束）", name());
}

void JobApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void JobApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void JobApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void JobApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_tick();
#endif
}

void JobApp::onBackgroundTick(std::uint32_t now_ms) {
  ++runs_;
  ELOG_INFO("job 后台任务：第 {} 轮跑完（now={} ms）—— 入口即将返回，槽位等 UI 任务回收", runs_,
            now_ms);
}

void JobApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

}  // namespace embark::demo
