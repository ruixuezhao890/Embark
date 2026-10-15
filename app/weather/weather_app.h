/**
 * Embark · 天气 App（薄壳）
 *
 * 界面 = AiDoGUI 屏 "weather"（屏名约定 == App 名，见 aidogui_ui_bridge.h）。屏上控件
 * 按名查找后直接写：
 *   L_City  城市名（14px）      L_NIcon 现在天气字（22px）   L_NTemp 现在温度（28px）
 *   L_NDesc 现在一句话（11px）   L_D{i}_Dow / _Icon / _Desc / _Range（i = 0..4，今天在 0）
 *   Btn_CityPrev / Btn_CityNext 左右切城市（lv_label 当按钮用，与 clock 屏翻月历同款）
 *
 * 数据来源 = 编译期桩 app/weather/weather_data.h（框架没有网络，ADR 0012）。城市选择存
 * hal().storage 的 "wx.city" 一个字节，下次启动还在那个城市；LauncherApp 的天气卡读同一
 * 个键同一张表，所以主屏卡片与天气屏永远一致。
 *
 * 入口有两条，都指到本 App：launcher 屏的天气卡（Card_Weather，设计器「调用函数」
 * embark_ui_open_weather）与主屏磁贴体系之外的任何界面控件都可照抄这个约定。
 *
 * 刷新纪律：只在"进屏 / 回屏 / 切城市"时写控件（数据是桩，不会自己变）；onForegroundTick
 * 只泵桥的 tick（AiDoGUI 侧是 no-op，没有变量层）。每次 lv_label_set_text 都是 LVGL 堆上
 * 一次 alloc + free，所以不做每帧重写。
 *
 * 后台策略：suspend（默认）—— 天气数据不会自己变，没必要占 tick。
 *
 * 薄壳纪律与其他 App 一致：不建屏、不 lv_scr_load，切换只经框架/界面事件。
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
  /// 给左右箭头挂点击回调（控件随屏重建，onEnter/onResume 都要重挂）。
  void bind_city_buttons();
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
