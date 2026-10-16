/**
 * Embark · 天气 App 实现。见 weather_app.h 的类注释。
 *
 * 桥调用的开关在 app/common/ui_backend.h：宿主构建（app/CMakeLists.txt）定义
 * EMBARK_AIDOGUI_UI，桥调用生效；没定义该宏的目标（esp32 真机，界面文件系统还没接）
 * 把桥调用编译成 no-op —— 本 App 的城市切换/持久化逻辑照常可跑。
 */
#include <cstdio>

#include <embark/framework.h>
#include <embark/log.h>

#include "weather_app.h"
#include "weather_data.h"

#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"
#include "ui_backend.h"
#endif

namespace embark::demo {

// repaint 回调是裸函数指针（不能捕获 this）：进程内只有这一个天气实例，文件级记下来。
WeatherApp* WeatherApp::g_for_ui = nullptr;

namespace {
constexpr const char* kScreen = "weather";
constexpr const char* kBtnPrev = "Btn_CityPrev";
constexpr const char* kBtnNext = "Btn_CityNext";
}  // namespace

void WeatherApp::onCreate(Framework& fw) {
  fw_ = &fw;
  city_ = weather_load_city(fw.hal().storage);  // 没有存过 → 0（北京）
  ELOG_INFO("App {} onCreate（城市 = {}，下标 {}；不申请后台）", name(), weather_city(city_).name, city_);
#if defined(EMBARK_AIDOGUI_UI)
  // 屏装载后重推控件值（ADR 0015）：屏切走即 destroy、回来即 init（ADR 0011 决策②），新控件
  // 上是设计器初值 —— repaint_screen() 负责重挂箭头 + 重画 5 天。
  g_for_ui = this;
  ui_backend_bind_repaint(kScreen, &WeatherApp::on_screen_reloaded);
#endif
}

void WeatherApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());  // 屏装载后由桥回调 repaint_screen()
#endif
}

void WeatherApp::onPause() {
  ELOG_INFO("App {} 退到后台（enter {}, resume {}）", name(), enters_, resumes_);
}

void WeatherApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());  // 同 onEnter：屏装载后由桥回调 repaint_screen()
#endif
}

void WeatherApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++foreground_ticks_;
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_tick();
#endif
}

void WeatherApp::onExit() {
  g_for_ui = nullptr;
  ELOG_INFO("App {} onExit", name());
}

void WeatherApp::on_screen_reloaded() {
  if (g_for_ui != nullptr) {
    g_for_ui->repaint_screen();
  }
}

void WeatherApp::repaint_screen() {
  // 屏是新的：箭头回调随控件消失（重挂），文案回到设计器初值（重画）。
  bind_city_buttons();
  refresh_display();
}

void WeatherApp::onMessage(const Message& msg) {
  (void)msg;  // 天气数据是桩，不订阅任何消息
}

const char* WeatherApp::city_name() const noexcept { return weather_city(city_).name; }

void WeatherApp::shift_city(int delta) {
  city_ = weather_shift_city(city_, delta);
  ++city_switches_;
  if (fw_ != nullptr) weather_save_city(fw_->hal().storage, city_);  // 下次启动还在这个城市
  ELOG_INFO("天气切到城市 {}（下标 {}，第 {} 次切换）", weather_city(city_).name, city_, city_switches_);
  refresh_display();
}

void WeatherApp::on_city_prev(lv_event_t* event) {
  if (auto* self = static_cast<WeatherApp*>(lv_event_get_user_data(event)); self != nullptr) {
    self->shift_city(-1);
  }
}

void WeatherApp::on_city_next(lv_event_t* event) {
  if (auto* self = static_cast<WeatherApp*>(lv_event_get_user_data(event)); self != nullptr) {
    self->shift_city(1);
  }
}

void WeatherApp::bind_city_buttons() {
#if defined(EMBARK_AIDOGUI_UI)
  // 控件随屏销毁消失，所以每次进屏都要重挂（与 clock 屏的翻月按钮同款）。
  if (lv_obj_t* prev = aidogui_ui_bridge_widget(kScreen, kBtnPrev); prev != nullptr) {
    lv_obj_add_event_cb(prev, &WeatherApp::on_city_prev, LV_EVENT_CLICKED, this);
  }
  if (lv_obj_t* next = aidogui_ui_bridge_widget(kScreen, kBtnNext); next != nullptr) {
    lv_obj_add_event_cb(next, &WeatherApp::on_city_next, LV_EVENT_CLICKED, this);
  }
#endif
}

void WeatherApp::set_label(const char* widget_name, const char* text) {
#if defined(EMBARK_AIDOGUI_UI)
  if (lv_obj_t* obj = aidogui_ui_bridge_widget(kScreen, widget_name); obj != nullptr) {
    lv_label_set_text(obj, text);
  }
#else
  (void)widget_name;
  (void)text;
#endif
}

void WeatherApp::refresh_display() {
#if defined(EMBARK_AIDOGUI_UI)
  if (!aidogui_ui_bridge_screen_created(kScreen)) {
    // 设计器里还没有 weather 屏（或缺屏）：保持当前屏 + 告警，别把界面写成半截。
    ELOG_INFO("天气屏 {} 未装载，跳过刷新", kScreen);
    return;
  }
  const WeatherCity& city = weather_city(city_);
  set_label("L_City", city.name);
  set_label("L_NIcon", city.days[0].icon);
  set_label("L_NTemp", city.temp);
  set_label("L_NDesc", city.days[0].desc);

  char widget[32];
  for (std::size_t i = 0; i < kWeatherDayCount; ++i) {
    const WeatherDay& day = city.days[i];
    std::snprintf(widget, sizeof widget, "L_D%u_Dow", static_cast<unsigned>(i));
    set_label(widget, day.dow);
    std::snprintf(widget, sizeof widget, "L_D%u_Icon", static_cast<unsigned>(i));
    set_label(widget, day.icon);
    std::snprintf(widget, sizeof widget, "L_D%u_Desc", static_cast<unsigned>(i));
    set_label(widget, day.desc);
    std::snprintf(widget, sizeof widget, "L_D%u_Range", static_cast<unsigned>(i));
    set_label(widget, day.range);
  }
  needs_refresh_ = false;
#endif
}

}  // namespace embark::demo
