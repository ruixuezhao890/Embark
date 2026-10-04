/**
 * Embark 示例 App 实现（issues/08）
 *
 * 三个 App 全程只做三件事：
 *   1. onCreate 里建自己的屏幕（lv_obj_create(nullptr) = 游离屏幕对象），
 *       ClockApp 还在其后启动状态机（chart_.start()：触发初始态的 on_entry）；
 *   2. onEnter/onResume 里 lv_scr_load 换上自己的屏幕（App 自决界面生命周期）；
 *   3. 想离开前台时 request_switch() 请求，框架在循环边界执行切换；
 *      App 之间只走消息（SettingsApp publish → ClockApp onMessage）。
 * 全程不碰框架内部，也绝不自己调 lv_timer_handler()（那是 UI 端口的活）。
 */
#include <cstdint>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "demo_apps.h"

namespace embark::demo {

namespace {

// 居中段落文字（宽度撑满 + 顶部居中，issue 05 同款样式）。
lv_obj_t* make_centered_label(lv_obj_t* parent, const char* text, lv_color_t color, lv_coord_t y) noexcept {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

// 带文字的标准按钮（默认主题蓝色，同 issue 05 的 Click me）。
lv_obj_t* make_button(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y) noexcept {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, demo_button_width, demo_button_height);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

}  // namespace

// ================================ ClockApp ================================
// 状态机（示范 §16.4）：led on / led off 两态，tick 事件来回切。
// 转移表（显式 current_state 版）：两个条目分别描述"on 态收 tick → off"与
// "off 态收 tick → on"，action 都是 on_tick（计数 + 刷界面）；状态的 on_entry
// 回调负责视觉副作用（换背景色）。事件不匹配时保持原状态（状态机丢弃该事件）。
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
    ClockApp::Chart::state(static_cast<Chart::state_id_t>(State::blinking_on), &ClockApp::enter_blinking_on),
    ClockApp::Chart::state(static_cast<Chart::state_id_t>(State::blinking_off), &ClockApp::enter_blinking_off),
};

ClockApp::ClockApp() noexcept
    : chart_(*this, kTransitions, kTransitions + 2, kStates, kStates + 2,
             static_cast<Chart::state_id_t>(State::blinking_on)) {}

void ClockApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = tick 100 ms）", name());

  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x121820), 0);

  make_centered_label(screen_, "Embark demo", lv_color_hex(0xf0f4f8), 20);
  make_centered_label(screen_, "clock app (tick + state_chart)", lv_color_hex(0x8a97a8), 50);

  state_label_ = make_centered_label(screen_, "Ticks: 0", lv_color_hex(0x6cd4ff), 85);
  refresh_state_label();
  brightness_label_ = make_centered_label(screen_, "Brightness: 0", lv_color_hex(0x54d97e), 110);

  lv_obj_t* settings_btn = make_button(screen_, "Settings", demo_button_x, demo_button_y);
  lv_obj_add_event_cb(settings_btn, &ClockApp::on_settings_button, LV_EVENT_CLICKED, this);

  // 状态机就位：触发初始态的 on_entry（背景换亮色），此后 tick 事件驱动往返。
  chart_.start();
}

void ClockApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_, enters_ + resumes_);
  lv_scr_load(screen_);
}

void ClockApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void ClockApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
}

void ClockApp::onExit() {
  ELOG_INFO("App {} onExit", name());
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
    // 消息驱动不进状态机：亮度档是"值"不是"行为"，直接更新界面（示例说明：
    // 不是所有输入都要过状态机，状态机表达周期性/时序行为）。
    const auto& brightness = static_cast<const BrightnessMessage&>(msg);
    level_ = brightness.level;
    refresh_brightness();
    ELOG_INFO("clock 收到 BrightnessMessage：亮度档 -> {}", level_);
  }
}

void ClockApp::on_tick() noexcept {
  ++ticks_;
  refresh_state_label();
}

void ClockApp::enter_blinking_on() noexcept {
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x1b3a5c), 0);  // led 亮：偏蓝
  ELOG_INFO("clock 状态机 -> led on");
}

void ClockApp::enter_blinking_off() noexcept {
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x121820), 0);  // led 灭：背景色
  ELOG_INFO("clock 状态机 -> led off");
}

void ClockApp::refresh_state_label() noexcept {
  lv_label_set_text_fmt(state_label_, "Ticks: %u (about %u s)", ticks_, ticks_ / 10);
}

void ClockApp::refresh_brightness() noexcept {
  lv_label_set_text_fmt(brightness_label_, "Brightness: %u", level_);
}

void ClockApp::on_settings_button(lv_event_t* event) noexcept {
  auto* self = static_cast<ClockApp*>(lv_event_get_user_data(event));
  const Error error = self->fw_->request_switch("settings");
  ELOG_INFO("请求切到 SettingsApp：{}", to_string(error));
}

// ================================ SettingsApp ================================

void SettingsApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = suspend）", name());

  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x201210), 0);

  make_centered_label(screen_, "Settings", lv_color_hex(0xf0f4f8), 20);
  make_centered_label(screen_, "suspend: foreground only", lv_color_hex(0x8a97a8), 50);

  brightness_label_ = make_centered_label(screen_, "Brightness: 0", lv_color_hex(0x6cd4ff), 85);
  refresh_brightness_label();

  lv_obj_t* level_btn = make_button(screen_, "Level +1", demo_button_x, demo_button_y);
  lv_obj_add_event_cb(level_btn, &SettingsApp::on_level_plus, LV_EVENT_CLICKED, this);

  lv_obj_t* back_btn = make_button(screen_, "Back to clock", demo_button_x, demo_switch_button_y);
  lv_obj_add_event_cb(back_btn, &SettingsApp::on_back, LV_EVENT_CLICKED, this);
}

void SettingsApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_, enters_ + resumes_);
  lv_scr_load(screen_);
}

void SettingsApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void SettingsApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
}

void SettingsApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void SettingsApp::on_level_plus(lv_event_t* event) noexcept {
  auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
  self->level_ = (self->level_ + 1U) % 4U;  // 四档循环：0..3
  self->refresh_brightness_label();
  // App 之间只走消息：广播给总线（ClockApp 的 onMessage 会收到并更新自己的档位）。
  self->fw_->publish(BrightnessMessage{static_cast<std::uint8_t>(self->level_)});
  ELOG_INFO("设置页：亮度档 -> {}（已广播 BrightnessMessage）", self->level_);
}

void SettingsApp::on_back(lv_event_t* event) noexcept {
  auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
  const Error error = self->fw_->request_switch("clock");
  ELOG_INFO("请求切回 ClockApp：{}", to_string(error));
}

void SettingsApp::refresh_brightness_label() noexcept {
  lv_label_set_text_fmt(brightness_label_, "Brightness: %u", level_);
}

// ================================ TickerApp ================================

void TickerApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（own_task，周期 50 ms，栈 256 字，优先级 4）", name());
}

void TickerApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
}

void TickerApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void TickerApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
}

void TickerApp::onBackgroundTick(std::uint32_t now_ms) {
  const std::uint32_t sequence = ++sent_;
  const AppId self_id = fw_->id_of(*this);
  ELOG_INFO("ticker 后台任务：发送第 {} 条（from_app={}，now={} ms）", sequence,
            static_cast<unsigned>(self_id), now_ms);
  // 跨任务投递：post() 只入收件箱（锁在队列内部），UI 循环在派发段广播。
  fw_->post(CrossTaskMessage(self_id, sequence));
}

void TickerApp::onMessage(const Message& msg) {
  if (msg.get_message_id() != cross_task_message_id) {
    return;
  }
  ++received_;
  const auto& envelope = static_cast<const CrossTaskMessage&>(msg);
  ELOG_INFO("UI 收到 ticker 消息：seq {}（from_app={}），累计收 {} 条", envelope.seq,
            static_cast<unsigned>(envelope.from_app), received_);
}

void TickerApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

}  // namespace embark::demo