/**
 * Embark 示例 App（issues/08，spec §14.1 的验收载体）
 *
 * 三个 App 各演示一种后台策略，凑齐 spec §14.1 的"≥2 个 App、可切前台、后台 tick
 * 可观测"，并顺带示范 spec §16.4 推荐的 etl::state_chart：
 *   - ClockApp（默认前台，后台策略 = tick，周期 100 ms）：
 *       闪烁时钟 —— 内部状态用 etl::state_chart 表达（led on/off 两个状态来回切，
 *       on_entry 回调换屏幕背景色）；每拍 +1 拍计数并在界面上刷新；
 *       收 SettingsApp 发来的 BrightnessMessage 更新亮度档（消息驱动，App 间不互
 *       include、不碰对方任何符号）。
 *   - SettingsApp（后台策略 = suspend）：纯前台交互页 —— 按钮 +1 亮度档后发消息
 *       广播；"Back to clock"按钮请求切回 ClockApp。退后台后完全不跑（suspend）。
 *   - TickerApp（后台策略 = own_task）：自己的 FreeRTOS 任务每 50 ms 发一条
 *       CrossTaskMessage 回 UI —— 证明"后台长活不阻塞 UI"与"消息经收件箱回 UI"
 *       （issue 07 的机制，原样迁移）。
 *
 * 界面的构建/装载纪律与 issue 06 一致：屏幕自己建（onCreate）、自己装
 * （onEnter/onResume），切换只经 fw.request_switch() 请求，框架在循环边界执行。
 * 界面文案是英文（LVGL 内置字体只有 Montserrat，无中文字形，issue 12 配字体）。
 *
 * 换后端不动本目录：这里只依赖 embark 公开头、LVGL 与 ETL（middleware），
 * 没有任何 platform/ 后端头（spec §14.5 验收项）。
 */
#ifndef EMBARK_APP_DEMO_APPS_H
#define EMBARK_APP_DEMO_APPS_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>
#include <middleware/etl/state_chart.h>

namespace embark::demo {

// --- 按钮几何（面板坐标；100×40，无人值守验收的合成点击锚点，ui_demo.cpp 引用）----
// ClockApp 的 "Settings" 与 SettingsApp 的 "Level +1" 用同一个中心 (160,170)：
// 同一坐标点落在不同 App 的屏幕上 —— 正好证明"输入焦点真的换了"。
inline constexpr int demo_button_x = 110;
inline constexpr int demo_button_y = 150;
inline constexpr int demo_button_width = 100;
inline constexpr int demo_button_height = 40;

inline constexpr int demo_click_center_x = demo_button_x + demo_button_width / 2;   // 160
inline constexpr int demo_click_center_y = demo_button_y + demo_button_height / 2;  // 170

// SettingsApp 的 "Back to clock" 按钮（在 "Level +1" 正下方）。
inline constexpr int demo_switch_button_y = demo_button_y + demo_button_height + 5;  // 195
inline constexpr int demo_switch_center_x = demo_click_center_x;                    // 160
inline constexpr int demo_switch_center_y = demo_switch_button_y + demo_button_height / 2;  // 215

/// 示例 App 之间的消息（App 间只走消息，不互相 include —— spec §7 / issue 08 验收）。
/// id 0x21 是 demo 私有区（框架保留 0xFE 给跨任务信封，勿撞）。
/// 注意：有基类（MessageT）就不是聚合了，必须给显式构造（ETL message 是平凡默认构造）。
struct BrightnessMessage : public MessageT<0x21> {
  constexpr BrightnessMessage(std::uint8_t level_value = 0) noexcept : level(level_value) {}
  std::uint8_t level = 0;  // 0..3 共四档
};

/// 示例 App 1：闪烁时钟。后台策略 = tick（100 ms）；内部状态 = etl::state_chart 示范。
class ClockApp final : public App {
 public:
  ClockApp() noexcept;

  [[nodiscard]] const char* name() const override { return "clock"; }

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
  void onMessage(const Message& msg) override;

  // --- 验收观测（非线程敏感：本 App 无 own task，全部在 UI 任务里跑）--------------
  [[nodiscard]] std::uint32_t ticks() const noexcept { return ticks_; }
  [[nodiscard]] std::uint32_t brightness() const noexcept { return level_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  // 状态机（示范 §16.4）：led on/off 两态 + tick 事件来回切；on_entry 负责换背景色。
  enum class State : std::uint8_t { blinking_on = 0, blinking_off = 1 };
  enum class Event : std::uint8_t { tick = 0 };

  using Chart = etl::state_chart<ClockApp>;  // TParameter = void（无参事件）
  static const Chart::transition kTransitions[2];  // 定义在 demo_apps.cpp
  static const Chart::state kStates[2];            // 定义在 demo_apps.cpp
  Chart chart_;

  void on_tick() noexcept;              // 状态机 action：++ticks_ + 刷标签
  void enter_blinking_on() noexcept;    // on_entry：背景换亮色
  void enter_blinking_off() noexcept;   // on_entry：背景换回深色
  void refresh_state_label() noexcept;
  void refresh_brightness() noexcept;
  static void on_settings_button(lv_event_t* event) noexcept;  // 请求切到 SettingsApp

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* state_label_ = nullptr;    // "Ticks: N (about N s)"
  lv_obj_t* brightness_label_ = nullptr;  // "Brightness: N"
  std::uint32_t ticks_ = 0;
  std::uint32_t level_ = 0;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

/// 示例 App 2：设置页。后台策略 = 默认（suspend）：纯前台交互，退后台完全不跑。
class SettingsApp final : public App {
 public:
  SettingsApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "settings"; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t level() const noexcept { return level_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  static void on_level_plus(lv_event_t* event) noexcept;  // 亮度 +1 → publish 消息
  static void on_back(lv_event_t* event) noexcept;        // 请求切回 ClockApp
  void refresh_brightness_label() noexcept;

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* brightness_label_ = nullptr;
  std::uint32_t level_ = 0;  // 演示消息：本 App 也持一份副本（观察用）
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

/// 示例 App 3：后台重活演示。后台策略 = own_task（issue 07 机制原样迁移）。
/// 自己的 FreeRTOS 任务每 50 ms 发一条 CrossTaskMessage 回 UI。
/// 观测线程纪律：sent_ 只被 own task 写，received_ 只被 UI 任务写（单写者，
/// 不需要原子；验收时在 UI 任务里读 sent_ 是观测性读取，v1 接受）。
class TickerApp final : public App {
 public:
  TickerApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "ticker"; }

  [[nodiscard]] AppSettings settings() const override {
    // own_task：50 ms 周期；栈 256 字（1 KB）；优先级 4（低于 UI 任务的 5）。
    return AppSettings{BackgroundPolicy::own_task, 50U, 256U, 4U};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onBackgroundTick(std::uint32_t now_ms) override;
  void onMessage(const Message& msg) override;
  void onExit() override;

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t sent() const noexcept { return sent_; }        // own task 线程
  [[nodiscard]] std::uint32_t received() const noexcept { return received_; }  // UI 任务线程

 private:
  Framework* fw_ = nullptr;
  std::uint32_t sent_ = 0;
  std::uint32_t received_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_DEMO_APPS_H */