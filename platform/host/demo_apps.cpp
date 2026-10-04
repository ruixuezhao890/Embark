/**
 * Embark 宿主 · 演示 App 实现（issues/06）
 *
 * 两个 App 全程只做三件事：
 *   1. onCreate 里建自己的屏幕（lv_obj_create(nullptr) = 游离屏幕对象）；
 *   2. onEnter/onResume 里 lv_scr_load 换上自己的屏幕（App 自决界面生命周期）；
 *   3. 想离开前台时 request_switch() 请求，框架在循环边界执行切换。
 * 全程不碰框架内部，也绝不自己调 lv_timer_handler()（那是 UI 端口的活）。
 */
#include <cstdint>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "demo_apps.h"

namespace embark::platform::host {

namespace {

// 演示用：把"前台次数"刷到界面上（文本是英文，LVGL 内置字体无中文）。
void set_foregrounds_text(lv_obj_t* label, std::uint32_t enters, std::uint32_t resumes) noexcept {
  lv_label_set_text_fmt(label, "Foregrounds: %u (enter %u / resume %u)", enters + resumes, enters, resumes);
}

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

// ================================ CounterApp ================================

void CounterApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate", name());
  build_screen();
}

void CounterApp::build_screen() noexcept {
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x121820), 0);

  make_centered_label(screen_, "Embark host UI", lv_color_hex(0xf0f4f8), 20);
  make_centered_label(screen_, "SDL2 + LVGL 8.3.11 via HAL", lv_color_hex(0x8a97a8), 50);

  clicks_label_ = make_centered_label(screen_, "Clicks: 0", lv_color_hex(0x6cd4ff), 85);
  foregrounds_label_ = make_centered_label(screen_, "Foregrounds: 0", lv_color_hex(0x54d97e), 110);

  lv_obj_t* click_me = make_button(screen_, "Click me", demo_button_x, demo_button_y);
  lv_obj_add_event_cb(click_me, &CounterApp::on_click_me, LV_EVENT_CLICKED, this);

  lv_obj_t* switch_btn = make_button(screen_, "Switch to app 2", demo_button_x, demo_switch_button_y);
  lv_obj_add_event_cb(switch_btn, &CounterApp::on_switch, LV_EVENT_CLICKED, this);
}

void CounterApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_, enters_ + resumes_);
  lv_scr_load(screen_);
  set_foregrounds_text(foregrounds_label_, enters_, resumes_);
}

void CounterApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void CounterApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
  set_foregrounds_text(foregrounds_label_, enters_, resumes_);
}

void CounterApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void CounterApp::on_click_me(lv_event_t* event) noexcept {
  auto* self = static_cast<CounterApp*>(lv_event_get_user_data(event));
  ++self->clicks_;
  lv_label_set_text_fmt(self->clicks_label_, "Clicks: %u", self->clicks_);
  ELOG_INFO("按钮点击：第 {} 次", self->clicks_);
}

void CounterApp::on_switch(lv_event_t* event) noexcept {
  auto* self = static_cast<CounterApp*>(lv_event_get_user_data(event));
  const Error err = self->fw_->request_switch("switch");
  ELOG_INFO("请求切换到 App 2：{}", to_string(err));
}

// ================================= SwitchApp =================================

void SwitchApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate", name());
  build_screen();
}

void SwitchApp::build_screen() noexcept {
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x201210), 0);

  make_centered_label(screen_, "App 2 in foreground", lv_color_hex(0xf0f4f8), 20);
  make_centered_label(screen_, "Input focus switched here", lv_color_hex(0x8a97a8), 50);

  foregrounds_label_ = make_centered_label(screen_, "Foregrounds: 0", lv_color_hex(0x54d97e), 85);

  // 与 CounterApp 的 Click me 相同位置 (160,170)：证明同一坐标在不同前台 App 上命中不同按钮。
  lv_obj_t* back = make_button(screen_, "Back to app 1", demo_button_x, demo_button_y);
  lv_obj_add_event_cb(back, &SwitchApp::on_back, LV_EVENT_CLICKED, this);
}

void SwitchApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
  set_foregrounds_text(foregrounds_label_, enters_, resumes_);
}

void SwitchApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void SwitchApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
  set_foregrounds_text(foregrounds_label_, enters_, resumes_);
}

void SwitchApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void SwitchApp::on_back(lv_event_t* event) noexcept {
  auto* self = static_cast<SwitchApp*>(lv_event_get_user_data(event));
  const Error err = self->fw_->request_switch("counter");
  ELOG_INFO("请求切回 App 1：{}", to_string(err));
}

// ================================= TickerApp =================================

// 后台 App（own_task 策略）：没有屏幕、不进前台，全程只有钩子与消息。
// onBackgroundTick 跑在它自己的 FreeRTOS 任务里（非 UI 任务），
// onMessage 跑在 UI 任务里 —— 线程切换点就是 fw_->post() 的收件箱。

void TickerApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（own_task，周期 {} ms，栈 {} 字，优先级 {}）", name(),
            settings().period_ms, settings().task_stack_words,
            static_cast<int>(settings().task_priority));
}

void TickerApp::onEnter() {
  ELOG_INFO("App {} 进入前台（不该发生：它是纯后台 App）", name());
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

}  // namespace embark::platform::host