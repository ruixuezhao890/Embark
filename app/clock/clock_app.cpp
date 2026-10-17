/**
 * Embark · 时钟 App 实现：见 clock_app.h 头注释。
 *
 * 薄壳纪律与 launcher_app.cpp / settings_app.cpp 完全一致：onCreate 只记下框架句柄 +
 * 声明"屏装载后重推控件值"；onEnter/onResume 只按屏名挂自己的界面屏；onForegroundTick
 * 读墙钟 → 算日历 → 把变化的文案写进界面控件，再泵一帧。没有后台节拍（ADR 0015）。
 *
 * AiDoGUI 后端（EMBARK_AIDOGUI_UI）：生成代码没有变量层，App 通过
 * aidogui_ui_bridge_widget(屏, 控件名) 拿 lv_obj_t* 直接写（ADR 0011 决策⑥）。
 * 月历是**真 lv_calendar 控件**，不是标签拼字符画：见 push_calendar()。
 *
 * 两个容易踩的点（都在下面代码里标注了）：
 *   1) 双色时间走 LVGL 的 recolor 语法：颜色前缀必须是 "#" + 恰好 6 位十六进制 +
 *      一个空格，且「切换颜色」要写双井号 "##RRGGBB "（单井号会被 PAR 态当转义符，
 *      把颜色码当正文画出来）。每行结尾还要补一个单井号把状态机拉回 WAIT，否则下一行
 *      的颜色前缀会变成可见垃圾。
 *      依据：third_party/lvgl/src/draw/lv_draw_label.c:244-285（状态机）与
 *      third_party/lvgl/src/misc/lv_txt.c:382-411（宽度路径里前缀不占宽度）。
 *      另外：生成代码不会替我们打开 recolor 开关（实测 export/screens/ui_clock.c 里没有
 *      lv_label_set_recolor），所以 push() 写控件前自己开。
 *   2) 每次写控件 = LVGL 堆上 1 次 alloc + 1 次 free（lv_label_set_text 会深拷文案），
 *      所以先跟缓存比、相同就不写；同一秒内也不重算。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "app_messages.h"
#include "clock_app.h"
#include "local_clock.h"

#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
#include "ui_backend.h"
#endif
#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"  // 迁移期旧链路：clock 的数据入口是 EEZ Flow 变量
#endif
#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"  // 控件表：clock 的数据入口是 AiDoGUI 屏 clock 的控件
#endif

namespace embark::demo {

// repaint 回调是裸函数指针（不能捕获 this）：进程内只有这一个时钟实例，文件级记下来。
ClockApp* ClockApp::g_for_ui = nullptr;

namespace {

// --- 星期名（本地时间口径见 local_clock.h：固定东八区 + 一天秒数）--------------
const char* const kWeekShort[7] = {"日", "一", "二", "三", "四", "五", "六"};

// --- 颜色（与原型 B_cards_feed.html 的配色令牌一致）---------------------------
// 只有双色时间还在用染色语法；月历格子的颜色由 lv_calendar 自己画（主题色描边），不可定制。
constexpr const char* kColMain = "e6ebf5";  // 主字
constexpr const char* kColBlue = "6ea0ff";  // 强调蓝（时间的冒号）
#if defined(EMBARK_AIDOGUI_UI)
// --- 数据出口：数据名 → AiDoGUI 屏 clock 里的控件名（ADR 0011 决策⑥）-------------
// App 只按名字写控件（aidogui_ui_bridge_widget），**不引**生成代码里的 ui_<id> 全局 —— 那是
// 绑定到具体控件的符号，设计器里改个名就静默断链。这张表是唯一的映射点：控件改名只改这里。
// var_name 沿用 EEZ 时代的 Flow 变量名（<app名>_<字段>），只当本文件内部的「哪个数据」键。
struct VarWidget {
  const char* var_name;
  const char* widget;  // AiDoGUI 里的控件名（设计器里的 name）
};

constexpr VarWidget kClockWidgets[] = {
    {"clock_time", "L_Time"},   {"clock_sw", "L_SW"},   {"clock_date", "L_Date"},
    {"clock_mh", "L_MH"},       {"clock_doy", "L_Doy"}, {"clock_p1", "L_P1"},
    {"clock_p2", "L_P2"},       {"clock_tm", "L_Tm"},   {"clock_tw", "L_Tw"},
    {"clock_dim", "L_Dim"},     {"clock_fd", "L_Fd"},   {"clock_fw", "L_Fw"},
    {"clock_prog", "Bar_Prog"},
};

constexpr const char* kClockScreen = "clock";   // 屏名 == App 名（约定，见 GLOSSARY.md）
constexpr const char* kCalendarWidget = "Cal";  // 月历卡里的真 lv_calendar 控件

/// 数据名 → 控件对象。屏没装载（还没进前台 / 已切走）时返回 nullptr。
lv_obj_t* clock_widget_for(const char* var_name) {
  for (const VarWidget& entry : kClockWidgets) {
    if (std::strcmp(entry.var_name, var_name) == 0) {
      return aidogui_ui_bridge_widget(kClockScreen, entry.widget);
    }
  }
  return nullptr;
}

/// 中文星期名（周日为一周之始，与 lv_calendar 默认的 "Su Mo …" 口径一致）。
// lv_calendar_set_day_names() 收 const char**（非 const 指针），所以这里不能用 const 数组。
const char* kCalendarDayNames[7] = {"日", "一", "二", "三", "四", "五", "六"};

/// 月历卡上的 < / > 翻月按钮（设计器里的 lv_label；lv_label 默认不可点击，运行时补 flag）。
constexpr const char* kBtnPrevWidget = "Btn_Prev";
constexpr const char* kBtnNextWidget = "Btn_Next";
#endif  // EMBARK_AIDOGUI_UI

[[nodiscard]] bool is_leap(int y) noexcept {
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

[[nodiscard]] int days_in_month(int y, int m) noexcept {
  static const int kDim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12)
    return 30;
  if (m == 2 && is_leap(y))
    return 29;
  return kDim[m - 1];
}

// floor_div / days_from_civil / civil_from_days / weekday_sun0 都在
// local_clock.h（launcher 的日期行也要用同一份换算，两处各写一份迟早对不上）。

[[nodiscard]] int day_of_year(int y, int m, int d) noexcept {
  int n = d;
  for (int i = 1; i < m; ++i)
    n += days_in_month(y, i);
  return n;
}

/// ISO 周（周一为一周之始）：年内第几天 + 该天是周几 → 周序号（可能为 0 或 53+）。
[[nodiscard]] int iso_week_raw(int y, int m, int d) noexcept {
  const std::int64_t days = days_from_civil(y, m, d);
  const int wd = ((weekday_sun0(days) + 6) % 7) + 1;  // 周一 = 1 … 周日 = 7
  return (day_of_year(y, m, d) - wd + 10) / 7;
}

/// 该年 ISO 周数：12-28 恒属本年最后一周（这是 ISO 8601 的判据）。
[[nodiscard]] int iso_weeks_in_year(int y) noexcept {
  return iso_week_raw(y, 12, 28);
}

[[nodiscard]] int iso_week(int y, int m, int d) noexcept {
  int w = iso_week_raw(y, m, d);
  if (w < 1)
    w = iso_weeks_in_year(y - 1);
  else if (w > iso_weeks_in_year(y))
    w = 1;
  return w;
}

// --- 月历控件（真 lv_calendar）--------------------------------------------------
// 设计器里月历卡放的是 lv_calendar 控件（AiDoGUI 的 specific 契约为空，没有「今天/显示月」
// 字段），所以这三件事只能在运行时做：
//   · set_day_names：默认星期名是英文 "Su Mo …"，换成中文（ui_heiti.ttf 有这些字形）；
//   · set_today_date / set_showed_date：推进「今天高亮」与显示的月份；
//   · 今天高亮是 lv_calendar 自己画的（draw_part_begin_event_cb 用主题主色描边），
//     格子底色/文字染色不可定制 —— 见 third_party/lvgl/src/extra/widgets/calendar/lv_calendar.c。
// 控件随屏重建会回到设计器初值（英文星期名、无高亮），所以 cal_names_ready_ / cal_today_*
// 由 repaint_screen() 清零，让紧接着的那次 refresh_display 重推一遍。
//
// 注：lvgl.h 只 include src/widgets/*，lv_calendar 在 src/extra/widgets/ 下，由
// src/extra/lv_extra.h → widgets/lv_widgets.h 带入（LVGL 8.3.11 已确认存在）。
}  // namespace

void ClockApp::onCreate(Framework& fw) {
  fw_ = &fw;  // 本 App 不 request_switch、不 publish，但要拿 hal().time 取墙钟
  ELOG_INFO("App {} onCreate（不申请后台；界面 = 生成的 clock 屏卡片流）", name());
  // 「返回」按钮 → 启动器：设计器里 Btn_Back 挂的是「调用函数」动作 embark_ui_open_launcher，
  // 落到 app/common/aidogui_ui_hooks.h 的 C 入口（见 ADR 0011 决策⑦），本 App 不声明导航。
#if defined(EMBARK_AIDOGUI_UI)
  // 屏装载后重推控件值（ADR 0015）：屏切走即 destroy、回来即 init（ADR 0011 决策②），新控件
  // 上是设计器初值 —— repaint_screen() 负责清缓存、重算、重挂翻月按钮。
  g_for_ui = this;
  ui_backend_bind_repaint("clock", &ClockApp::on_screen_reloaded);
#endif
}

void ClockApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}, 累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  // 装载自己的屏；屏装好后桥回调 repaint_screen()：清缓存（保留视图月）、重算、重挂翻月按钮。
  ui_backend_load_for_app(name());
#endif
  // 屏没被重载（上一个 App 没有同名屏）时桥不会回调 repaint，但下一个前台帧就会自己重算
  // （onForegroundTick 逐帧调 refresh_display），所以这里不需要额外兜底。
}

void ClockApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void ClockApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());  // 同 onEnter：屏装载后由桥回调 repaint_screen()
#endif
}

void ClockApp::onExit() {
  g_for_ui = nullptr;
  ELOG_INFO("App {} onExit", name());
}

void ClockApp::on_screen_reloaded() {
  if (g_for_ui != nullptr) {
    g_for_ui->repaint_screen();
  }
}

void ClockApp::repaint_screen() {
  // 屏是新的：控件全在设计器初值上，而 DisplayCache 还留着上次的值 —— 不清空的话 push 会
  // 认为"值没变"直接跳过，屏上就停在假数据。月历同理：lv_calendar 重建后今天高亮和中文
  // 星期名都没了，cal_* 缓存一起清零，让下面这次 refresh_display 重推一遍。
  // **cal_view_y_/cal_view_m_ 不清**：那是用户翻到的视图月，属于要留存的视图状态。
  shown_ = DisplayCache{};
  shown_prog_ = -1;
  last_second_ = 0xFFFFFFFFU;
  cal_today_y_ = 0;
  cal_today_m_ = 0;
  cal_today_d_ = 0;
  cal_shown_y_ = 0;
  cal_shown_m_ = 0;
  cal_names_ready_ = false;
  if (fw_ != nullptr) {
    refresh_display(fw_->hal().time.now_ms());
  }
  // 屏是重建的：< / > 翻月按钮的点击回调随控件一起没了，重挂一次。
  bind_calendar_buttons();
}

void ClockApp::onForegroundTick(std::uint32_t now_ms) {
  ++foreground_ticks_;
  // 前台逐帧刷新：时间与日历只有人在看时才需要推进（ADR 0015）。refresh_display 自己按秒
  // 短路，所以每帧成本 = 一次整数比较；真变了才写控件。
  refresh_display(now_ms);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  // 泵一帧。EEZ 后端 = ui_tick()（lv_timer_handler + tick_screen(当前屏)：绑了 Flow
  // 全局变量的控件在那里读到最新值）；AiDoGUI 后端 = 空实现（生成代码没有变量层，
  // 控件值由 App 通过 aidogui_ui_bridge_widget() 直接写）。见 app/common/ui_backend.h。
  ui_backend_tick();
#endif
}

void ClockApp::onMessage(const Message& msg) {
  if (msg.get_message_id() == BrightnessMessage::ID) {
    // 消息只是“值”不是“行为”：收到就直接更新数据（界面显示由下一帧
    // onForegroundTick → refresh_display 写控件完成）。需要“收消息即切状态”的 App，见
    // docs/guides/state-machine-and-messages.md。
    const auto& brightness = static_cast<const BrightnessMessage&>(msg);
    level_ = brightness.level;
    ELOG_INFO("clock 收到 BrightnessMessage：亮度档 -> {}", level_);
  }
}

void ClockApp::push(const char* var_name, const char* value, char* cache, std::size_t cap) {
  if (std::strcmp(cache, value) == 0)
    return;  // 相同值重写 = 白费一次 alloc/free
  const std::size_t n = std::strlen(value);
  const std::size_t copy = (n < cap - 1) ? n : cap - 1;
  std::memcpy(cache, value, copy);
  cache[copy] = '\0';
#if defined(EMBARK_AIDOGUI_UI)
  if (lv_obj_t* widget = clock_widget_for(var_name); widget != nullptr) {
    // 文案里的 "#rrggbb 文本#" 是 LVGL 的染色标记，生成代码不会替我们打开开关（实测
    // export/screens/ui_clock.c 里没有 lv_label_set_recolor），所以写之前自己开。
    lv_label_set_recolor(widget, true);
    lv_label_set_text(widget, cache);
  }
  // 屏没装载（还没进前台 / 已切走）时控件是 nullptr：静默跳过，值留在 cache 里；屏下次
  // 装载时 repaint_screen() 会清缓存重算，所以不会丢（也不会读到旧屏的对象）。
#elif defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_set_var_string(var_name, cache);
#else
  (void)var_name;  // 真机还没接界面时桥是 no-op（屏也不存在），计算照跑
#endif
}

void ClockApp::push_int(const char* var_name, std::int32_t value, std::int32_t& cache) {
  if (cache == value)
    return;
  cache = value;
#if defined(EMBARK_AIDOGUI_UI)
  if (lv_obj_t* widget = clock_widget_for(var_name); widget != nullptr) {
    lv_bar_set_value(widget, value, LV_ANIM_OFF);  // Bar_Prog：第 N 天进度条
  }
#elif defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_set_var_int(var_name, value);
#else
  (void)var_name;
#endif
}

void ClockApp::write_placeholder() {
  // 墙钟不可用（真机 RTC 未接）时的占位：照原型的 “--” 初值，绝不显示 1970 年。
  push("clock_time", "#e6ebf5 --##6ea0ff :##e6ebf5 --", shown_.time, sizeof shown_.time);
  push("clock_sw", "等待对时", shown_.sw, sizeof shown_.sw);
  push("clock_date", "----年--月--日 星期-", shown_.date, sizeof shown_.date);
  push("clock_mh", "----年--月", shown_.mh, sizeof shown_.mh);
  // 月历是控件不是文案：墙钟没来时它自己就是设计器初值（本月、无高亮、英文星期名），
  // 没有「占位」可写 —— 中文星期名仍然注入（控件存在就生效，与墙钟无关）。
#if defined(EMBARK_AIDOGUI_UI)
  push_calendar(0, 0, 0);
#endif
  push("clock_p1", "-- / -- 天", shown_.p1, sizeof shown_.p1);
  push_int("clock_prog", 0, shown_prog_);
  push("clock_p2", "-- 天", shown_.p2, sizeof shown_.p2);
  push("clock_tm", "--月--日", shown_.tm, sizeof shown_.tm);
  push("clock_tw", "周-", shown_.tw, sizeof shown_.tw);
  push("clock_dim", "--", shown_.dim, sizeof shown_.dim);
  push("clock_fd", "周-", shown_.fd, sizeof shown_.fd);
  push("clock_fw", "第 -- 周", shown_.fw, sizeof shown_.fw);
}

void ClockApp::refresh_display(std::uint32_t now_ms) {
  (void)now_ms;
  if (fw_ == nullptr)
    return;

  const auto epoch = fw_->hal().time.epoch_ms();
  if (!epoch.has_value()) {
    // 契约（include/embark/hal/time.h）：没有 RTC / 没对时返回 unsupported，不是致命
    // 错误，调用方必须自己处理。占位文案幂等 —— 值没变时 push 会自己跳过。
    if (clock_ready_)
      ELOG_INFO("clock 墙钟不可用（{}），界面转占位文案", epoch.error());
    clock_ready_ = false;
    write_placeholder();
    return;
  }

  const std::int64_t local_s =
      static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
  const std::int64_t days = floor_div(local_s, kSecondsPerDay);
  const int sod = static_cast<int>(local_s - days * kSecondsPerDay);
  const int sec = sod % 60;
  if (clock_ready_ && static_cast<std::uint32_t>(sec) == last_second_)
    return;  // 同一秒不重算
  last_second_ = static_cast<std::uint32_t>(sec);

  int y = 0;
  int m = 0;
  int d = 0;
  civil_from_days(days, y, m, d);

  const int hh = sod / 3600;
  const int mm = (sod / 60) % 60;
  const int wd = weekday_sun0(days);
  const int dim = days_in_month(y, m);

  if (!clock_ready_)
    ELOG_INFO("clock 拿到墙钟：{}-{}-{} {}:{}:{}", y, m, d, hh, mm, sec);
  clock_ready_ = true;

  char buf[64];

  // 卡 1：时间。冒号单独染色 —— 单井号起色、双井号换色（见文件头「1)」）。
  std::snprintf(buf, sizeof buf, "#%s %02d##%s :##%s %02d", kColMain, hh, kColBlue, kColMain, mm);
  push("clock_time", buf, shown_.time, sizeof shown_.time);

  // 分隔符用全角逗号：ui_heiti.ttf 没有 U+00B7「·」的字形，画出来是方框（实测）。
  std::snprintf(buf, sizeof buf, "秒 %02d，第 %d 周", sec, iso_week(y, m, d));
  push("clock_sw", buf, shown_.sw, sizeof shown_.sw);

  std::snprintf(buf, sizeof buf, "%04d年%d月%d日 周%s", y, m, d, kWeekShort[wd]);
  push("clock_date", buf, shown_.date, sizeof shown_.date);

  // 卡 2：月历。标题与格子都按「视图月」更新（默认 = 今天；用户翻月后两者一起切到
  // 显示月），全在 push_calendar 里做 —— 标题 push 挪过去是为了翻月时同拍联动。
  // 月历是真的 lv_calendar 控件、由 aidogui 桥取到：没有该后端的构建（esp32 真机当前
  // 状态）里 push_calendar 不参与编译（定义处同受本宏保护），调用点也得一起挡掉。
#if defined(EMBARK_AIDOGUI_UI)
  push_calendar(y, m, d);
#endif

  const std::int64_t first_days = days_from_civil(y, m, 1);

  // 卡 3：今日。
  std::snprintf(buf, sizeof buf, "%d", day_of_year(y, m, d));
  push("clock_doy", buf, shown_.doy, sizeof shown_.doy);

  std::snprintf(buf, sizeof buf, "%d / %d 天", d, dim);
  push("clock_p1", buf, shown_.p1, sizeof shown_.p1);

  push_int("clock_prog", static_cast<std::int32_t>((d * 100) / dim), shown_prog_);

  std::snprintf(buf, sizeof buf, "%d 天", dim - d);
  push("clock_p2", buf, shown_.p2, sizeof shown_.p2);

  // 卡 4：明日（跨月/跨年由 civil 往返自然处理，不用手写进位）。
  int ty = 0;
  int tm = 0;
  int td = 0;
  civil_from_days(days + 1, ty, tm, td);
  std::snprintf(buf, sizeof buf, "%d月%d日", tm, td);
  push("clock_tm", buf, shown_.tm, sizeof shown_.tm);
  std::snprintf(buf, sizeof buf, "周%s", kWeekShort[weekday_sun0(days + 1)]);
  push("clock_tw", buf, shown_.tw, sizeof shown_.tw);

  // 卡 5：本月。
  std::snprintf(buf, sizeof buf, "%d", dim);
  push("clock_dim", buf, shown_.dim, sizeof shown_.dim);
  std::snprintf(buf, sizeof buf, "周%s", kWeekShort[weekday_sun0(first_days)]);
  push("clock_fd", buf, shown_.fd, sizeof shown_.fd);
  std::snprintf(buf, sizeof buf, "第 %d 周", iso_week(y, m, 1));
  push("clock_fw", buf, shown_.fw, sizeof shown_.fw);
}

#if defined(EMBARK_AIDOGUI_UI)
void ClockApp::push_calendar(int y, int m, int d) {
  // 显示月 = 视图月（用户翻过月就不跟今天走）；视图月成员是 0 = 跟随今天。
  const int vy = (cal_view_y_ > 0 && cal_view_m_ > 0) ? cal_view_y_ : y;
  const int vm = (cal_view_y_ > 0 && cal_view_m_ > 0) ? cal_view_m_ : m;

  lv_obj_t* cal = aidogui_ui_bridge_widget(kClockScreen, kCalendarWidget);
  if (cal != nullptr) {
    // 中文星期名：控件每次重建都会退回英文默认值，所以不靠 cal_names_ready_ 做全局一次性。
    lv_calendar_set_day_names(cal, kCalendarDayNames);
  }

  // y/m/d 为 0 = 墙钟不可用：只注入星期名，不推进「今天 / 显示月」（留着设计器初值）。
  if (y <= 0 || m <= 0)
    return;

  if (cal != nullptr) {
    // 今天只在跨天时动（高亮描边重算）；显示月单独按 (vy, vm) 推进 —— 用户翻月后
    // 视图月与今天脱钩，set_today_date 与 set_showed_date 各管各的，互不拉回。
    const bool new_today = (y != cal_today_y_ || m != cal_today_m_ || d != cal_today_d_);
    const bool new_month = (vy != cal_shown_y_ || vm != cal_shown_m_);
    if (new_today) {
      lv_calendar_set_today_date(cal, static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(m),
                                 static_cast<std::uint32_t>(d));
      cal_today_y_ = y;
      cal_today_m_ = m;
      cal_today_d_ = d;
    }
    if (new_month) {
      // set_showed_date 会重填整个 btnmatrix（48 次 set_text），所以只在换月时才动。
      lv_calendar_set_showed_date(cal, static_cast<std::uint32_t>(vy),
                                  static_cast<std::uint32_t>(vm));
      cal_shown_y_ = vy;
      cal_shown_m_ = vm;
    }
  }

  // 卡 2 标题按显示月（不是今天）更新：翻月时标题与格子同拍切换。
  char buf[24];
  std::snprintf(buf, sizeof buf, "%d年%d月", vy, vm);
  push("clock_mh", buf, shown_.mh, sizeof shown_.mh);
}

void ClockApp::bind_calendar_buttons() {
  lv_obj_t* prev = aidogui_ui_bridge_widget(kClockScreen, kBtnPrevWidget);
  lv_obj_t* next = aidogui_ui_bridge_widget(kClockScreen, kBtnNextWidget);
  if (prev == nullptr || next == nullptr) {
    return;  // 屏没装载 / 设计器里还没有这两个按钮：静默跳过（onEnter/onResume 会重试）
  }
  // lv_label 构造时清掉了 CLICKABLE（lv_label.c:716），必须运行时补上才收得到点击。
  lv_obj_add_flag(prev, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(next, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(prev, &ClockApp::on_calendar_prev, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(next, &ClockApp::on_calendar_next, LV_EVENT_CLICKED, this);
}

void ClockApp::shift_calendar_month(int delta) {
  if (fw_ == nullptr)
    return;
  const auto epoch = fw_->hal().time.epoch_ms();
  if (!epoch.has_value())
    return;  // 墙钟不可用：没有「今天」可参照，不翻

  const std::int64_t local_s =
      static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
  const std::int64_t days = floor_div(local_s, kSecondsPerDay);
  int ty = 0;
  int tm = 0;
  int td = 0;
  civil_from_days(days, ty, tm, td);

  // 基准 = 当前视图月；从未翻过（0）就按今天起跳。delta 带年进位（跨 1/12 月）。
  int vy = (cal_view_y_ > 0 && cal_view_m_ > 0) ? cal_view_y_ : ty;
  int vm = (cal_view_y_ > 0 && cal_view_m_ > 0) ? cal_view_m_ : tm;
  vm += delta;
  while (vm > 12) {
    vm -= 12;
    ++vy;
  }
  while (vm < 1) {
    vm += 12;
    --vy;
  }
  cal_view_y_ = vy;
  cal_view_m_ = vm;

  // 显示月变了：清掉 cal_shown_* 让 push_calendar 必然重填 btnmatrix，并同步推一次
  // （不要调 refresh_display：它有「同秒直接返回」的短路，同秒内翻月会被吞掉）。
  cal_shown_y_ = 0;
  cal_shown_m_ = 0;
  ELOG_INFO("clock 月历翻到视图月 {}-{}", vy, vm);
  push_calendar(ty, tm, td);
}

void ClockApp::on_calendar_prev(lv_event_t* event) {
  auto* self = static_cast<ClockApp*>(lv_event_get_user_data(event));
  if (self != nullptr)
    self->shift_calendar_month(-1);
}

void ClockApp::on_calendar_next(lv_event_t* event) {
  auto* self = static_cast<ClockApp*>(lv_event_get_user_data(event));
  if (self != nullptr)
    self->shift_calendar_month(+1);
}
#endif  // EMBARK_AIDOGUI_UI

}  // namespace embark::demo
