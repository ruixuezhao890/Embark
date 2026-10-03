/**
 * Embark 宿主 · 演示 App（issues/06）
 *
 * 两个 App 证明 spec §5/§6 的机制：
 *   - CounterApp（默认前台）：issue 05 的界面（计数 + 点击）+ 一个"切到 App 2"按钮；
 *   - SwitchApp：自己的界面 + "切回 App 1"按钮，同时展示 onEnter/onResume 的计数。
 *
 * 两个 App 都遵守 App 契约：屏幕自己建（onCreate）、自己装（onEnter/onResume），
 * 前台切换只通过 fw.request_switch() 请求，框架在循环边界执行 —— 本文件不直接
 * 调任何 LVGL 的屏幕切换 API 之外的框架接口。
 *
 * 界面文案是英文（LVGL 内置字体只有 Montserrat，无中文字形，issue 12 配字体）。
 * 按钮几何是无人值守验收的合成点击目标（ui_demo.cpp 引用），改布局要同步改那。
 *
 * 注意：这是平台侧的演示代码，不是框架内核；内核与 App 契约（include/embark/）
 * 不依赖 LVGL —— 界面 App 用 lvgl.h 是天经地义的（UI 任务 = App 的舞台）。
 */
#ifndef EMBARK_PLATFORM_HOST_DEMO_APPS_H
#define EMBARK_PLATFORM_HOST_DEMO_APPS_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>

namespace embark::platform::host {

// --- 按钮几何（面板坐标；100×40，与 issue 05 的演示一致）----------------------
// CounterApp 的"Click me"与 SwitchApp 的"Back to app 1"用同一个中心 (160,170)：
// 这正好能证明"输入焦点真的换了" —— 同一坐标，点在不同 App 的屏幕上。
inline constexpr int demo_button_x = 110;
inline constexpr int demo_button_y = 150;
inline constexpr int demo_button_width = 100;
inline constexpr int demo_button_height = 40;

inline constexpr int demo_click_center_x = demo_button_x + demo_button_width / 2;   // 160
inline constexpr int demo_click_center_y = demo_button_y + demo_button_height / 2;  // 170

// CounterApp 的"切到 App 2"按钮（在 Click me 正下方）。
inline constexpr int demo_switch_button_y = demo_button_y + demo_button_height + 5;  // 195
inline constexpr int demo_switch_center_x = demo_click_center_x;                    // 160
inline constexpr int demo_switch_center_y = demo_switch_button_y + demo_button_height / 2;  // 215

/// 演示 App 1：计数标签 + "Click me" + "Switch to app 2"。
/// 后台策略 = 默认（suspend）：v1 的演示 App 都是纯前台逻辑。
class CounterApp final : public App {
 public:
  CounterApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "counter"; }
  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t clicks() const noexcept { return clicks_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  // LVGL 事件回调 = C 函数指针，所以是 static 成员；this 从 lv_event_get_user_data 取。
  static void on_click_me(lv_event_t* event) noexcept;    // 计数 +1
  static void on_switch(lv_event_t* event) noexcept;      // 请求切到 SwitchApp
  void build_screen() noexcept;

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* clicks_label_ = nullptr;   // 界面上的 "Clicks: N" 文本
  lv_obj_t* foregrounds_label_ = nullptr;  // "Foregrounds: N"（看着 onEnter/onResume 动）
  std::uint32_t clicks_ = 0;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

/// 演示 App 2：自己的界面 + 回退按钮；证明切换 = 换了一批界面 + 输入焦点。
class SwitchApp final : public App {
 public:
  SwitchApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "switch"; }
  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  static void on_back(lv_event_t* event) noexcept;  // 请求切回 CounterApp（LVGL 回调约束同上）
  void build_screen() noexcept;

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* foregrounds_label_ = nullptr;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_DEMO_APPS_H */