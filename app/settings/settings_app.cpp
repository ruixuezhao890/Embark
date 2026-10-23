/**
 * Embark · 设置中心实现：见 settings_app.h 头注释。
 *
 * 一个 repaint 钩子干两件事：刷文本（亮度百分比 / 时间值 / 当前字段 / 关于七行）+ 重挂五个
 * 按钮的 LV_EVENT_CLICKED。控件随屏 destroy 一起消失（ADR 0011 决策②），所以每次装载都得重来。
 *
 * 为什么不用 ui_backend_bind_action：那张表只有 8 个槽（app/common/aidogui_ui_nav.cpp），
 * 而设置页已经 5 个按钮、以后还要加。绑定表是给「跨 App / 跨屏跳转」用的，屏内交互归 App。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <embark/framework.h>
#include <embark/hal/display.h>
#include <embark/log.h>
#include <embark/version.h>

#include "app_messages.h"
#include "local_clock.h"
#include "settings_app.h"
#include "settings_prefs.h"

#if defined(EMBARK_AIDOGUI_UI)
#include "ui_backend.h"
#endif
#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"  // 控件查表 + 字库记账
#endif

namespace embark::demo {
namespace {

constexpr const char* kScreen = "settings";

/// 屏内按钮的动作种类。加一个按钮 = 这里加一行 + kSinks 加一行 + dispatcher 加一个分支。
enum class SinkKind : std::uint8_t {
  bright_minus,
  bright_plus,
  time_minus,
  time_plus,
  time_next,
};

/// 一条「控件名 → 动作」。注册时把 &kSinks[i] 当 user_data 交给 LVGL，回调里再取回来 ——
/// 不做「整数转指针」那种实现定义的转换，也不为每个按钮写一个重复的小函数。
struct Sink {
  const char* widget;
  SinkKind kind;
};

constexpr Sink kSinks[] = {
    {"Btn_Minus", SinkKind::bright_minus},
    {"Btn_Plus", SinkKind::bright_plus},
    {"Btn_TimeMinus", SinkKind::time_minus},
    {"Btn_TimePlus", SinkKind::time_plus},
    {"Btn_TimeNext", SinkKind::time_next},
};

/// 时间字段名，下标即 time_field_。顺序也是「下一字段」的顺序：年 → 月 → 日 → 时 → 分。
constexpr const char* kFieldNames[] = {"年", "月", "日", "时", "分"};
constexpr std::size_t kFieldCount = sizeof kFieldNames / sizeof kFieldNames[0];

/// 屏上的按钮回调是裸函数指针（不能捕获 this），用一个静态指针找回**当前**实例：
/// 同一时刻只有前台的 SettingsApp 会装自己的屏。
SettingsApp* g_settings_for_ui = nullptr;

/// 把 value 环绕进 [lo, hi]（日按月天数、时 0..23、分 0..59 都靠它）。
[[nodiscard]] int wrap(int value, int lo, int hi) noexcept {
  const int span = hi - lo + 1;
  int r = (value - lo) % span;
  if (r < 0) {
    r += span;
  }
  return lo + r;
}

[[nodiscard]] bool is_leap_year(int y) noexcept {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/// 当月天数（改「年」/「月」之后要把 29~31 日钳回来，不然会造出 2025-02-29）。
[[nodiscard]] int days_in_month(int y, int m) noexcept {
  constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) {
    return 31;
  }
  if (m == 2 && is_leap_year(y)) {
    return 29;
  }
  return kMonthDays[m - 1];
}

/// 字节 → KB（四舍五入）。堆/栈的数字是给「够不够用」看的，字节精度没有意义；
/// 而且 7 位数字塞进 188px 的行会把「KB」两个字挤出去。
[[nodiscard]] std::size_t to_kb(std::size_t bytes) noexcept { return (bytes + 512U) / 1024U; }

#if defined(EMBARK_AIDOGUI_UI)
/// 屏内按钮总入口：一个回调 + 一张表。
void on_sink_clicked(lv_event_t* event) {
  if (g_settings_for_ui == nullptr) {
    return;
  }
  const auto* sink = static_cast<const Sink*>(lv_event_get_user_data(event));
  if (sink == nullptr) {
    return;
  }
  switch (sink->kind) {
    case SinkKind::bright_minus:
      g_settings_for_ui->bump_brightness(-kBrightStep);
      break;
    case SinkKind::bright_plus:
      g_settings_for_ui->bump_brightness(kBrightStep);
      break;
    case SinkKind::time_minus:
      g_settings_for_ui->nudge_time_field(-1);
      break;
    case SinkKind::time_plus:
      g_settings_for_ui->nudge_time_field(1);
      break;
    case SinkKind::time_next:
      g_settings_for_ui->next_time_field();
      break;
  }
}

/// 五个按钮挂上（每次屏装载后调用：控件已经换了一批新的，旧指针不能留）。
void bind_sinks() {
  for (const Sink& sink : kSinks) {
    if (lv_obj_t* obj = aidogui_ui_bridge_widget(kScreen, sink.widget); obj != nullptr) {
      lv_obj_add_event_cb(obj, &on_sink_clicked, LV_EVENT_CLICKED,
                          const_cast<Sink*>(&sink));
    }
  }
}
#endif  // EMBARK_AIDOGUI_UI

}  // namespace

void SettingsApp::onCreate(Framework& fw) {
  fw_ = &fw;
  const hal::Context& h = fw.hal();
  // 1) 读 KV：没存过 = 默认 70%。存的是「用户上次调成多少」，与 HAL 当前值无关。
  percent_ = load_bright(h.storage);
  // 2) 立刻写进 HAL：onCreate 早于总线装配（core/src/embark/framework.cpp 有注释明写），
  //    publish 出去没人收，所以「开机亮度」只能靠直接调 HAL。
  if (h.display != nullptr) {
    (void)h.display->set_backlight(percent_);
  }
  ELOG_INFO("App {} onCreate（亮度 {}%，`后台策略 = suspend；界面 = 屏 {}）", name(),
            static_cast<unsigned>(percent_), kScreen);
#if defined(EMBARK_AIDOGUI_UI)
  g_settings_for_ui = this;
  // 屏切走即 destroy、回来即 init（ADR 0011 决策②），新控件上是设计器初值；
  // 刷文本 + 重挂按钮都在这个钩子里做（ADR 0015）。
  ui_backend_bind_repaint(kScreen, &SettingsApp::on_screen_reloaded);
#endif
}

void SettingsApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());  // 屏装载后由桥回调 refresh_display()
#endif
}

void SettingsApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void SettingsApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());  // 同 onEnter
#endif
}

void SettingsApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void SettingsApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_tick();
#endif
}

void SettingsApp::on_screen_reloaded() {
  if (g_settings_for_ui != nullptr) {
    g_settings_for_ui->refresh_display();
  }
}

void SettingsApp::bump_brightness(int delta) {
  if (fw_ == nullptr) {
    return;
  }
  // 两端钳位：0 再减还是 0，100 再加还是 100（不是四档循环）。
  percent_ = clamp_percent(static_cast<int>(percent_) + delta);
  const hal::Context& h = fw_->hal();
  if (h.display != nullptr) {
    (void)h.display->set_backlight(percent_);
  }
  fw_->publish(BrightnessMessage{percent_});
  save_bright(h.storage, percent_);  // 写失败只当没存上，不挡操作
  ELOG_INFO("设置页：亮度 -> {}%（HAL 已写 + BrightnessMessage 已广播 + KV 已落）",
            static_cast<unsigned>(percent_));
  write_brightness_text();
}

void SettingsApp::nudge_time_field(int delta) {
  if (fw_ == nullptr) {
    return;
  }
  // 每次点击都现读墙上钟，不留草稿态：改到一半退出屏，改过的字段也已经生效了。
  const auto epoch = fw_->hal().time.epoch_ms();
  if (!epoch.has_value()) {
    // 没有 RTC（或后端不支持/没对时）：按钮点了没反应，不弹窗不 assert，界面写「本机无 RTC」。
    time_ok_ = false;
    write_time_text();
    return;
  }
  const std::int64_t local_s =
      static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
  const std::int64_t days = floor_div(local_s, kSecondsPerDay);
  int y = 0;
  int m = 0;
  int d = 0;
  civil_from_days(days, y, m, d);
  const std::int64_t sod = local_s - days * kSecondsPerDay;
  int hh = static_cast<int>(sod / 3600);
  int mi = static_cast<int>((sod / 60) % 60);
  const int ss = static_cast<int>(sod % 60);

  switch (time_field_) {
    case 0:  // 年
      y += delta;
      break;
    case 1:  // 月（不动年份：12 月 +1 回到 1 月，想跨年就再点「年」）
      m = wrap(m + delta, 1, 12);
      break;
    case 2:  // 日（0 号 = 上月最后一天的口径太绕，直接在本月内环绕）
      d += delta;
      break;
    case 3:  // 时
      hh = wrap(hh + delta, 0, 23);
      break;
    default:  // 分
      mi = wrap(mi + delta, 0, 59);
      break;
  }

  const int dim = days_in_month(y, m);
  if (time_field_ == 2U) {
    d = wrap(d, 1, dim);
  } else if (d > dim) {
    d = dim;  // 改年/月后 29~31 日可能不存在 → 钳到当月最后一天（2024-02-29 → 2025-02-28）
  }

  const std::int64_t next_local_s =
      days_from_civil(y, m, d) * kSecondsPerDay + hh * 3600 + mi * 60 + ss;
  const auto wrote = fw_->hal().time.set_epoch_ms(
      static_cast<std::uint64_t>(next_local_s - kLocalOffsetSeconds) * 1000U);
  time_ok_ = (wrote == Error::none);
  ELOG_INFO("设置页：改{} 后 -> {:04}-{:02}-{:02} {:02}:{:02}（写回墙上钟 {}）",
            kFieldNames[time_field_], y, m, d, hh, mi, time_ok_ ? "成功" : "失败");
  write_time_text();
}

void SettingsApp::next_time_field() {
  time_field_ = static_cast<std::uint8_t>((static_cast<std::size_t>(time_field_) + 1U) % kFieldCount);
  write_time_text();
}

void SettingsApp::select_time_field(std::uint32_t index) {
  time_field_ = static_cast<std::uint8_t>(
      static_cast<std::size_t>(index) < kFieldCount ? index : 0U);
  write_time_text();
}

void SettingsApp::refresh_display() {
#if defined(EMBARK_AIDOGUI_UI)
  if (!aidogui_ui_bridge_screen_created(kScreen)) {
    // 设计器里缺屏：保持当前屏 + 告警，别把界面写成半截。
    ELOG_INFO("设置屏 {} 未装载，跳过刷新", kScreen);
    return;
  }
  if (fw_ == nullptr) {
    return;
  }
  ++refreshes_;
  bind_sinks();  // 控件是新的一批，事件必须先重挂（否则按钮全哑）
  write_brightness_text();
  write_time_text();
  write_about_rows();
#endif
}

void SettingsApp::set_label(const char* widget_name, const char* text) {
#if defined(EMBARK_AIDOGUI_UI)
  if (lv_obj_t* obj = aidogui_ui_bridge_widget(kScreen, widget_name); obj != nullptr) {
    lv_label_set_text(obj, text);
  }
#else
  (void)widget_name;
  (void)text;
#endif
}

void SettingsApp::set_row(const char* widget_name, const char* key, const char* value) {
  char text[64];
  std::snprintf(text, sizeof text, "%s：%s", key, value);
  set_label(widget_name, text);
}

void SettingsApp::write_brightness_text() {
  char text[8];
  std::snprintf(text, sizeof text, "%u%%", static_cast<unsigned>(percent_));
  set_label("L_BrightVal", text);
}

void SettingsApp::write_time_text() {
  set_label("L_TimeField", kFieldNames[time_field_]);
  if (fw_ == nullptr) {
    return;
  }
  const auto epoch = fw_->hal().time.epoch_ms();
  if (!epoch.has_value()) {
    time_ok_ = false;
    set_label("L_TimeVal", "本机无 RTC");
    return;
  }
  time_ok_ = true;
  const std::int64_t local_s =
      static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
  const std::int64_t days = floor_div(local_s, kSecondsPerDay);
  int y = 0;
  int m = 0;
  int d = 0;
  civil_from_days(days, y, m, d);
  const std::int64_t sod = local_s - days * kSecondsPerDay;
  char text[32];
  std::snprintf(text, sizeof text, "%d-%02d-%02d %02d:%02d", y, m, d,
                static_cast<int>(sod / 3600), static_cast<int>((sod / 60) % 60));
  set_label("L_TimeVal", text);
}

void SettingsApp::write_about_rows() {
#if defined(EMBARK_AIDOGUI_UI)
  const hal::Context& h = fw_->hal();
  char value[48];

  // 1) 平台 / 版本：函数取，同一份 C++ 在宿主报 host、在真机报 esp32。
  set_row("L_Platform", "平台", platform_name());
  set_row("L_Version", "版本", version_string());

  // 2) 可用堆：宿主后端不量（返回 0），真机报 KB。
  const std::size_t free_heap = h.system.free_heap_bytes();
  if (free_heap != 0U) {
    std::snprintf(value, sizeof value, "%u KB", static_cast<unsigned>(to_kb(free_heap)));
    set_row("L_Heap", "可用堆", value);
  } else {
    set_row("L_Heap", "可用堆", "量不了");
  }

  // 3) 字库记账：设计器导出 7 份，池里装上了几份（真机 SD 缺字库时 loaded < total）。
  const AidoguiFontReport font = aidogui_ui_bridge_font_report();
  std::snprintf(value, sizeof value, "%d/%d 份", font.loaded, font.total);
  set_row("L_Font", "字库", value);

  // 4) 栈水位：宿主不量（返回 0）——单位用 KB，真机的栈水位是几千字节量级。
  const std::size_t stack = h.system.stack_high_water_bytes();
  if (stack != 0U) {
    std::snprintf(value, sizeof value, "%u KB", static_cast<unsigned>(to_kb(stack)));
    set_row("L_Stack", "栈水位", value);
  } else {
    set_row("L_Stack", "栈水位", "量不了");
  }

  // 5) 存盘：没有文件系统后端（宿主默认、真机 EMBARK_SD_ENABLED=0）就是未挂载。
  set_row("L_Disk", "存盘", h.filesystem != nullptr ? "已挂载" : "未挂载");

  // 6) 对时：epoch_ms() 没有 RTC/没对时就写「未对时」，不猜。
  const auto epoch = h.time.epoch_ms();
  if (epoch.has_value()) {
    const std::int64_t local_s =
        static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
    const std::int64_t days = floor_div(local_s, kSecondsPerDay);
    int y = 0;
    int m = 0;
    int d = 0;
    civil_from_days(days, y, m, d);
    const std::int64_t sod = local_s - days * kSecondsPerDay;
    std::snprintf(value, sizeof value, "%d-%02d-%02d %02d:%02d", y, m, d,
                  static_cast<int>(sod / 3600), static_cast<int>((sod / 60) % 60));
    set_row("L_Clock", "对时", value);
  } else {
    set_row("L_Clock", "对时", "未对时");
  }

  // 一行汇总日志：串口上一眼能看出七行推成了什么（宿主没有堆/栈，会是「量不了」）。
  ELOG_INFO("设置页刷新（第 {} 次）：{} / 堆 {} / 字库 {}/{} / 存盘 {} / 对时 {} / 亮度 {}%",
            refreshes_, platform_name(), free_heap != 0U ? "有" : "量不了", font.loaded,
            font.total, h.filesystem != nullptr ? "已挂载" : "未挂载",
            epoch.has_value() ? "有" : "未对时", static_cast<unsigned>(percent_));
#endif
}

}  // namespace embark::demo