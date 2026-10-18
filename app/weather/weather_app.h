/**
 * Embark · 天气 App（薄壳）
 *
 * 界面 = AiDoGUI 屏 "weather"（屏名约定 == App 名，见 aidogui_ui_bridge.h）。屏上控件
 * 按名查找后直接写：
 *   L_City  城市名（14px）      L_NIcon 现在天气字（22px）   L_NTemp 现在温度（28px）
 *   L_NDesc 现在一句话（11px）   L_D{i}_Dow / _Icon / _Desc / _Range（i = 0..4，今天在 0）
 *   Btn_CityPrev / Btn_CityNext 左右切城市（lv_label 当按钮用，与 clock 屏翻月历同款）
 *
 * 数据来源 = 编译期桩 app/weather/weather_data.h（网络服务尚未落地，ADR 0012 / 0019）。城市选择存
 * hal().storage 的 "wx.city" 一个字节，下次启动还在那个城市；LauncherApp 的天气卡读同一
 * 个键同一张表，所以主屏卡片与天气屏永远一致。
 *
 * 入口有两条，都指到本 App：launcher 屏的天气卡（Card_Weather，设计器「调用函数」
 * embark_ui_open_weather）与主屏磁贴体系之外的任何界面控件都可照抄这个约定。
 *
 * 刷新纪律：数据是桩、不会自己变，所以只在"屏装载后 / 切城市"时写控件 —— 屏装载后的那次
 * 由桥回调 repaint_screen()（onCreate 里声明一次，见 aidogui_ui_nav_bind_repaint / ADR 0015），
 * 不再由 onEnter/onResume 手工调用；onForegroundTick 只泵桥的 tick（AiDoGUI 侧是 no-op）。
 * 每次 lv_label_set_text 都是 LVGL 堆上一次 alloc + free，所以不做每帧重写。
 *
 * 后台策略：suspend（默认）—— 天气数据不会自己变，没必要占 tick。
 *
 * 薄壳纪律与其他 App 一致：不建屏、不 lv_scr_load，切换只经框架/界面事件；onCreate 只记下
 * 框架句柄 + 声明"屏装载后重推控件值"。
 */
#ifndef EMBARK_APP_WEATHER_APP_H
#define EMBARK_APP_WEATHER_APP_H

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class WeatherApp final : public App {
 public:
  WeatherApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "weather"; }
  [[nodiscard]] const char* title() const override { return "天气"; }
  // 图标 = LV_SYMBOL_TINT（雨滴）；主屏现在是 AiDoGUI 画的，这个只用于框架侧兜底展示。
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_TINT; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onMessage(const Message& msg) override;

  // --- 验收观测（--ui-weather 断言用；全部在 UI 任务里跑）------------------------
  [[nodiscard]] std::size_t city_index() const noexcept { return city_; }
  [[nodiscard]] const char* city_name() const noexcept;
  [[nodiscard]] unsigned enters() const noexcept { return enters_; }
  [[nodiscard]] unsigned resumes() const noexcept { return resumes_; }
  [[nodiscard]] unsigned city_switches() const noexcept { return city_switches_; }
  [[nodiscard]] unsigned foreground_ticks() const noexcept { return foreground_ticks_; }

 private:
  /// 把当前城市的 5 天与"现在"写进 weather 屏（屏没装载 → 静默跳过）。
  void refresh_display();
  /// 屏装载后的重推钩子（桥在每次装载完新屏后调用，见 aidogui_ui_nav_bind_repaint）：
  /// 屏每次都是新对象 —— 重挂左右箭头、重画 5 天与"现在"。
  void repaint_screen();
  /// 给左右箭头挂点击回调（控件随屏重建，repaint_screen() 里重挂）。
  void bind_city_buttons();
  /// bind_repaint 收的是裸函数指针（不能捕获 this）：转发到当前实例。
  static void on_screen_reloaded();
  static WeatherApp* g_for_ui;  // 当前接线的天气实例（进程内只有一个）
  /// 切城市：改下标 → 落盘 → 立刻重画。
  void shift_city(int delta);
  static void on_city_prev(lv_event_t* event);
  static void on_city_next(lv_event_t* event);
  /// 按名写一个 label（控件不存在时静默跳过）。
  static void set_label(const char* widget_name, const char* text);

  Framework* fw_ = nullptr;  // onCreate 记下；hal().storage 存取所选城市
  std::size_t city_ = 0U;
  bool needs_refresh_ = true;  // 屏刚建过 = 控件是设计器初值，必须重画
  unsigned enters_ = 0U;
  unsigned resumes_ = 0U;
  unsigned city_switches_ = 0U;
  unsigned foreground_ticks_ = 0U;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_WEATHER_APP_H */
