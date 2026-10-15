/**
 * Embark · 时钟 App（薄壳）。后台策略 = tick（100 ms）：onBackgroundTick 里读墙钟、把
 * 时间与日历算出来写进界面控件（AiDoGUI 屏“clock”按名查找，见 aidogui_ui_bridge.h）。
 * 数据出口：L_Time / L_SW / L_Date / L_MH / Cal / L_Doy / L_P1 / Bar_Prog / L_P2 /
 * L_Tm / L_Tw / L_Dim / L_Fd / L_Fw。
 *
 * 界面 = AiDoGUI 屏“clock”（屏名约定：screen 名 == App 名，子页 <app名>_<编号>_sub），
 * 版式对应原型 lvgl_screen_by_ai/clock_ui/B_cards_feed.html 的卡片流：
 *   卡 1 时间（L_Time / L_SW）、卡 2 日期（L_Date）、卡 3 月历（L_MH + 真 lv_calendar
   控件 Cal）、卡 4 年进度（L_Doy / Bar_Prog / L_P1 / L_P2）、卡 5 明日（L_Tm / L_Tw）、
   卡 6 本月（L_Dim / L_Fd / L_Fw）。
 * 日历算法全部在 C++ 侧（本月天数、1 号是周几、年内第几天、ISO 周数）；月历不是用
   标签拼字符画，而是真 lv_calendar 控件，本 App 只调
   lv_calendar_set_today_date / set_showed_date / set_day_names（中文星期名）。
 *
 * 时间来源：hal::ITime::epoch_ms()。宿主用 system_clock（能跑）；真机板载 RTC
 * （PCF85063）v1 还没接，esp32 后端返回 Error::unsupported —— 那时本 App 显示
 * “等待对时”占位，而不是把 0 当成 1970 年（契约见 include/embark/hal/time.h）。
 *
 * 刷新纪律：100 ms 一拍，但同一秒内不重算；算出的文案与上一拍相同也不写控件 ——
 * 每次 lv_label_set_text 都是 LVGL 堆上 1 次 alloc + 1 次 free，所以只有值真变了
 * 才动界面。
 *
 * 消息：收 SettingsApp 广播的 BrightnessMessage（id 0x21，定义在 app/common/app_messages.h）
 * 更新亮度档 —— 消息驱动，App 间不互相 include、不碰对方任何符号（spec §7）。
 *
 * 薄壳纪律（与 launcher_app / settings_app 完全一致，2026-10-06 目录化后各 App 独立成目录）：
 *   - App 不建屏、不 lv_scr_load；onCreate 只记账/启后台逻辑；
 *   - onEnter/onResume 只做一件事：按屏名约定装载自己的屏
       （aidogui_ui_bridge_load_screen_for_app —— 设计器还没画同名屏时保持当前屏 +
        告警，缺屏 App 策略 A：画好即自动生效）；
 *   - onForegroundTick 泵 aidogui_ui_bridge_tick（AiDoGUI 侧是 no-op：没有变量层，
       控件值由 App 直接写）；
 *   - 切换只经 fw.request_switch() 请求；回主屏由 clock 屏的 Btn_Back「调用函数」
       事件 embark_ui_open_launcher 承担（导航壳 2026-10-06 退役）；
 *   - App 之间只走消息（SettingsApp publish → 本 App onMessage）。
 *
 * 编译开关 EMBARK_AIDOGUI_UI（新）/ EMBARK_EEZ_UI_BRIDGE（迁移期旧链路）：宿主构建
 * （app/CMakeLists.txt 定义）接界面生成代码；未定义的目标（esp32 真机）把桥调用编译成
 * no-op —— App 逻辑（节拍/日历/消息）照常可跑，真机接入界面后打开开关即可。
 *
 * 换后端不动本目录：这里只依赖 embark 公开头、LVGL（头符号 + lv_calendar）与界面薄桥，
 * 没有任何 platform/ 后端头（spec §14.5 验收项）。
 */
#ifndef EMBARK_APP_CLOCK_APP_H
#define EMBARK_APP_CLOCK_APP_H

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

/// 示例 App：时钟 + 日历（薄壳）。后台策略 = tick（100 ms）；界面 = AiDoGUI 屏“clock”。
class ClockApp final : public App {
 public:
  ClockApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "clock"; }
  [[nodiscard]] const char* title() const override { return "时钟"; }
  // 图标 = LV_SYMBOL_REFRESH（0xF021，字库已含）；accent 用默认。
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_REFRESH; }

  [[nodiscard]] AppSettings settings() const override {
    // tick 策略：周期 100 ms。（宿主 UI 循环 5 ms 一拍 → 每 20 拍一颗 tick。）
    return AppSettings{BackgroundPolicy::tick, 100U, 0U, 0U};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onBackgroundTick(std::uint32_t now_ms) override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onMessage(const Message& msg) override;

  // --- 验收观测（本 App 无 own task，全部在 UI 任务里跑，非线程敏感）--------------
  [[nodiscard]] std::uint32_t ticks() const noexcept { return ticks_; }
  [[nodiscard]] std::uint32_t brightness() const noexcept { return level_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }
  [[nodiscard]] std::uint32_t foreground_ticks() const noexcept { return foreground_ticks_; }
  /// 墙钟可用（epoch_ms 有值）—— 真机 RTC 未接时恒 false，界面显示“等待对时”。
  [[nodiscard]] bool clock_ready() const noexcept { return clock_ready_; }

 private:
  /// 上次写进界面的文案：相同就不重写（省一次 LVGL 堆 alloc/free）。
  struct DisplayCache {
    char time[40];
    char sw[48];
    char date[48];
    char mh[24];
    char doy[16];
    char p1[24];
    char p2[16];
    char tm[24];
    char tw[16];
    char dim[16];
    char fd[16];
    char fw[24];
  };

  /// 一拍：取墙钟 → 算日历 → 把变化的文案写进界面控件。
  void refresh_display(std::uint32_t now_ms);
  /// 墙钟不可用时写一次占位文案（原型里的 “--” 初值）。
  void write_placeholder();
  /// 月历控件：注入中文星期名 + 推进“今天 / 显示月”（控件不存在时静默跳过）。
  /// 显示月 = 视图月（cal_view_y_/cal_view_m_，0 表示跟随今天）。
  void push_calendar(int y, int m, int d);
  /// 给 < / > 翻月按钮挂点击回调（控件随屏重建，onEnter/onResume 都要重挂）。
  void bind_calendar_buttons();
  /// 视图月 +delta（带年进位；改完立即重推控件）。
  void shift_calendar_month(int delta);
  /// 翻月按钮的点击回调（static，user_data = ClockApp*，由 bind_calendar_buttons 挂上）。
  static void on_calendar_prev(lv_event_t* event);
  static void on_calendar_next(lv_event_t* event);
  /// 值变化才写控件（相同值重写 = 白费一次 LVGL 堆 alloc/free）。
  void push(const char* var_name, const char* value, char* cache, std::size_t cap);
  void push_int(const char* var_name, std::int32_t value, std::int32_t& cache);

  Framework* fw_ = nullptr;  // onCreate 记下；hal().time 是取墙钟的唯一入口
  DisplayCache shown_{};
  std::int32_t shown_prog_ = -1;
  // 月历控件（lv_calendar）上次推进的“今天 / 显示月”。控件随屏重建，回到设计器初值，
  // 所以 onEnter/onResume 里要把它们清零，让下一拍重推一遍。
  int cal_today_y_ = 0;
  int cal_today_m_ = 0;
  int cal_today_d_ = 0;
  int cal_shown_y_ = 0;
  int cal_shown_m_ = 0;
  // 视图月（用户翻月后的“显示月”）：0/0 = 跟随今天。与今天的年月不同时，
  // 卡 2 标题与 lv_calendar 都按视图月显示；点按钮只动它，不动今天。
  int cal_view_y_ = 0;
  int cal_view_m_ = 0;
  bool cal_names_ready_ = false;  // 中文星期名是否已注入当前控件
  std::uint32_t ticks_ = 0;
  std::uint32_t level_ = 0;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
  std::uint32_t foreground_ticks_ = 0;
  std::uint32_t last_second_ = 0xFFFFFFFFU;  // 上次刷新的秒（同秒不重算）
  bool clock_ready_ = false;                  // 墙钟可用（状态翻转时才打日志）
};

}  // namespace embark::demo

#endif /* EMBARK_APP_CLOCK_APP_H */
