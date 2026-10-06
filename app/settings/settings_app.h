/**
 * Embark · 设置 App（薄壳）。后台策略 = suspend（默认值，示范最小设置）；界面 = EEZ 屏 “settings”。
 *
 * 本 App 展示两种跨层数据出口：
 *   1) 亮度档 level_：改动经 SettingsApp::bump_level()（0..3 循环 +1），广播 BrightnessMessage
 *      （id 0x21，定义在 app/common/app_messages.h）给 ClockApp —— 消息驱动，App 间不互相 include；
 *   2) UI 呈现一律交给 EEZ 屏绑定 Flow 全局变量（C++ 不再碰控件）。
 *
 * 薄壳纪律（与 launcher_app / clock_app 完全一致，2026-10-06 目录化后各 App 独立成目录）：
 *   - App 不建屏、不 lv_scr_load；onCreate 只记账（本 App 顺带持有框架句柄以备 publish）；
 *   - onEnter/onResume 只按屏名加载自己的 EEZ 屏（缺屏 App 策略 A：保持当前屏 + 告警）；
 *   - onForegroundTick 泵 eez_ui_bridge_tick；
 *   - 切 App 只经 fw.request_switch()；回主屏由 EEZ 屏按钮 SetPage 回 launcher 屏承担
 *       （导航壳 2026-10-06 退役）；
 *   - App 之间只走消息（本 App publish BrightnessMessage）。
 *
 * 编译开关 EMBARK_EEZ_UI_BRIDGE：宿主构建（app/CMakeLists.txt 定义）接 EEZ 生成代码；
 * 未定义的目标（esp32 真机）把桥调用编译成 no-op —— 本 App 逻辑（bump_level/广播）照常可跑。
 */
#ifndef EMBARK_APP_SETTINGS_APP_H
#define EMBARK_APP_SETTINGS_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

/// 示例 App：设置（亮度档）。后台策略 = suspend（最小设置样板）；界面 = EEZ 屏 “settings”。
class SettingsApp final : public App {
 public:
  SettingsApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "settings"; }
  [[nodiscard]] const char* title() const override { return "设置"; }
  // 图标 = LV_SYMBOL_SETTINGS（0xE001，字库已含）；accent 用默认。
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_SETTINGS; }

  [[nodiscard]] AppSettings settings() const override {
    // 最小设置：全默认值（suspend 策略：不进后台循环）。这示范“用默认就是正确”。
    return AppSettings{};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

  /// 亮度档 +1（0..3 循环）；广播 BrightnessMessage。EEZ Flow 动作/验收程序调用。
  void bump_level();

  // --- 验收观测（全部在 UI 任务里跑，非线程敏感）------------------------------
  [[nodiscard]] std::uint32_t level() const noexcept { return level_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  Framework* fw_ = nullptr;
  std::uint32_t level_ = 0;  // 0..3 共四档
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_SETTINGS_APP_H */
