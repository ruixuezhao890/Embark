/**
 * Embark · 设置中心 App（薄壳）。后台策略 = suspend（默认值）；界面 = AiDoGUI 屏 “settings”。
 *
 * 三条原则（ADR 0018）：
 *   1) 亮度真值在 HAL：本 App 只持一份 percent_ 镜像（0..100），用来显示与落 KV；
 *      真改亮度走 hal().display->set_backlight()。开机时 onCreate 读 KV 再写回 HAL ——
 *      onCreate 早于总线装配，所以「开机生效」不能靠 publish 消息（framework.cpp 有注释明写）；
 *   2) 设置真值放 KV（键 ui.bright 等，见 settings_prefs.h），不建 SettingsService：
 *      设置项彼此无关，集中服务只会让每加一项都得改服务；
 *   3) 屏内交互归 App：按钮用 lv_obj_add_event_cb 挂在屏上，不进 ui_backend_bind_action 的
 *      8 个槽（设置页有 5 个按钮，以后还会更多）。控件随屏 destroy 一起消失，所以事件与
 *      文本刷新都在 repaint 钩子里重做（ADR 0015）—— 一个钩子干两件事。
 *
 * 首屏三块：显示（亮度 − / ＋）、时间（年月日时分逐字段 −/＋/下一字段，每次点击立刻写回
 * 墙上钟，没有「保存」按钮也没有草稿态）、关于本机（七行只读：平台/版本/堆/字库/栈/存盘/对时）。
 *
 * 薄壳纪律（与 launcher_app / clock_app / album_app 一致）：
 *   - App 不建屏、不 lv_scr_load；onCreate 只记账（本 App 顺带读 KV + 写 HAL）；
 *   - onEnter/onResume 只按屏名加载自己的 AiDoGUI 屏（缺屏 App 策略 A：保持当前屏 + 告警）；
 *   - onForegroundTick 泵一帧 ui_backend_tick()；
 *   - 切 App 只经 fw.request_switch()；回主屏由屏上的「返回」按钮承担（设计器事件
 *     embark_ui_open_launcher，见 ADR 0014）；
 *   - App 之间只走消息（本 App publish BrightnessMessage，ClockApp 收）。
 */
#ifndef EMBARK_APP_SETTINGS_APP_H
#define EMBARK_APP_SETTINGS_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

/// 设置中心：亮度百分比 + 手动设时间 + 关于本机。后台策略 = suspend；界面 = AiDoGUI 屏 “settings”。
class SettingsApp final : public App {
 public:
  SettingsApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "settings"; }
  [[nodiscard]] const char* title() const override { return "设置"; }
  // 图标 = LV_SYMBOL_SETTINGS（0xE001，字库已含）；accent 用默认。
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_SETTINGS; }

  [[nodiscard]] AppSettings settings() const override {
    // 全默认值（suspend 策略：不进后台循环）。这示范「用默认就是正确」。
    return AppSettings{};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

  // --- 逻辑入口（屏内 dispatcher 与验收程序共用；不造触摸事件也能驱动）----------------

  /// 亮度 ±delta，两端钳到 0..100；写 HAL → 广播 → 落 KV → 刷屏。
  void bump_brightness(int delta);

  /// 当前时间字段 ±delta（按当月天数与 23:59 环绕钳位），立刻写回墙上钟。
  void nudge_time_field(int delta);

  /// 切到下一个时间字段（0 年 → 1 月 → 2 日 → 3 时 → 4 分 → 0）。
  void next_time_field();

  /// 直接选时间字段（0..4，越界钳位）—— 验收程序用，不点屏也能把「日」摆到边界上试。
  void select_time_field(std::uint32_t index);

  /// 屏装载后的统一入口：刷文本 + 重挂按钮事件（由 ui_backend_bind_repaint 调）。
  static void on_screen_reloaded();

  // --- 验收观测（全部在 UI 任务里跑，非线程敏感）------------------------------
  [[nodiscard]] std::uint8_t brightness_percent() const noexcept { return percent_; }
  [[nodiscard]] std::uint32_t time_field() const noexcept { return time_field_; }
  [[nodiscard]] bool time_ok() const noexcept { return time_ok_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }
  [[nodiscard]] std::uint32_t refreshes() const noexcept { return refreshes_; }

 private:
  void refresh_display();
  void write_brightness_text();
  void write_time_text();
  void write_about_rows();
  void set_label(const char* widget_name, const char* text);
  void set_row(const char* widget_name, const char* key, const char* value);

  Framework* fw_ = nullptr;
  std::uint8_t percent_ = 70U;     // 亮度镜像，0..100（真值在 HAL）
  std::uint8_t time_field_ = 0U;  // 0 年 / 1 月 / 2 日 / 3 时 / 4 分
  bool time_ok_ = true;           // 上一次读写墙上钟是否成功（无 RTC → false）
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
  std::uint32_t refreshes_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_SETTINGS_APP_H */