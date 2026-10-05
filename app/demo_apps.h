/**
 * Embark 示例 App（issues/08，spec §14.1 的验收载体；issue 16 起带启动器元数据）
 *
 * 四个 App 各演示一种后台策略，凑齐 spec §14.1 的"≥2 个 App、可切前台、后台 tick
 * 可观测"，并顺带示范 spec §16.4 推荐的 etl::state_chart：
 *   - LauncherApp（主屏/默认前台，后台策略 = suspend，见 launcher_app.h）：扇形主屏，
 *       拖动/点按/按键选槽，点选中槽 request_switch 启动 —— issue 16 本体。
 *   - ClockApp（后台策略 = tick，周期 100 ms）：
 *       闪烁时钟 —— 内部状态用 etl::state_chart 表达（led on/off 两个状态来回切，
 *       on_entry 回调换屏幕背景色）；每拍 +1 拍计数并在界面上刷新；
 *       收 SettingsApp 发来的 BrightnessMessage 更新亮度档（消息驱动，App 间不互
 *       include、不碰对方任何符号）。
 *   - SettingsApp（后台策略 = suspend）：纯前台交互页 —— 按钮 +1 亮度档后发消息
 *       广播。回主屏走导航壳的返回键（框架 request_home），自己不画"返回"按钮
 *       （issue 16 / ADR 0006：切换只走 request_switch 与 request_home）。
 *   - TickerApp（后台策略 = own_task）：自己的 FreeRTOS 任务每 50 ms 发一条
 *       CrossTaskMessage 回 UI —— 证明"后台长活不阻塞 UI"与"消息经收件箱回 UI"
 *       （issue 07 的机制，原样迁移）。
 *   - JobApp（后台策略 = own_task，period_ms = 0）：一次性任务 —— 入口跑一轮
 *       onBackgroundTick 就返回；平台记 finished 并 park，框架在 step 的回收段归还槽位，
 *       于是"创建 → 跑完 → 回收 → 再创建"能反复走（issue 15 的任务生命周期）。
 *       它只挂在系统用例（platform/host/ui_tour.cpp）的注册表里。
 *   - EezDemoApp（EEZ Studio 生成 UI，issue 19 / ADR 0008）：见 eez_demo_app.h。
 *
 * App 元数据（issue 16）：title() 中文标题 / icon() LV_SYMBOL 码点（可选）/
 * accent() 强调色（可选，默认全局强调色），随 EMBARK_APP_TABLE 编译期注册，
 * 启动器（LauncherApp）拿它们画槽：图标优先，没有图标取标题首个汉字兜底。
 * 元数据只是虚函数覆写 —— 没有第二张表和 App 表对账，不存在"表长漂移"。
 *
 * 界面的构建/装载纪律与 issue 06 一致：屏幕自己建（onCreate）、自己装
 * （onEnter/onResume），切换只经 fw.request_switch() 请求，框架在循环边界执行。
 * 启动器与导航壳（返回键/状态行）用静态子集字库 embark_zh_14（tools/font/ 生成，
 * 含这里的全部中文标题 + LV_SYMBOL 码点）；demo App 自己的界面文字沿用英文
 * （LVGL 默认 Montserrat，v1 不为此扩展字库）。
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

// --- 按钮几何（面板坐标；140×40，无人值守验收的合成点击锚点，ui_demo.cpp 引用）----
// 竖屏 240×320：按钮水平居中（x = (240-140)/2 = 50），落在下半屏。
// ClockApp 的 "Settings" 与 SettingsApp 的 "Level +1" 用同一个中心 (120,220)：
// 同一坐标点落在不同 App 的屏幕上 —— 正好证明"输入焦点真的换了"。
// （SettingsApp 曾有的 "Back to clock" 按钮随 issue 16 移除：回主屏走导航壳返回键，
// 中心点 (120,265) 一并删除。）
inline constexpr int demo_button_x = 50;
inline constexpr int demo_button_y = 200;
inline constexpr int demo_button_width = 140;
inline constexpr int demo_button_height = 40;

inline constexpr int demo_click_center_x = demo_button_x + demo_button_width / 2;   // 120
inline constexpr int demo_click_center_y = demo_button_y + demo_button_height / 2;  // 220

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

  using Chart = etl::state_chart<ClockApp>;        // TParameter = void（无参事件）
  static const Chart::transition kTransitions[2];  // 定义在 demo_apps.cpp
  static const Chart::state kStates[2];            // 定义在 demo_apps.cpp
  Chart chart_;

  void on_tick() noexcept;             // 状态机 action：++ticks_ + 刷标签
  void enter_blinking_on() noexcept;   // on_entry：背景换亮色
  void enter_blinking_off() noexcept;  // on_entry：背景换回深色
  void refresh_state_label() noexcept;
  void refresh_brightness() noexcept;
  static void on_settings_button(lv_event_t* event) noexcept;  // 请求切到 SettingsApp

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* state_label_ = nullptr;       // "Ticks: N (about N s)"
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
  [[nodiscard]] const char* title() const override { return "设置"; }
  // 图标 = LV_SYMBOL_SETTINGS（0xF013，字库已含）。
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_SETTINGS; }

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
  [[nodiscard]] const char* title() const override { return "心跳"; }
  // 无图标：启动器取标题首字符"心"兜底（字库已含"心跳"两字）。

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
  [[nodiscard]] std::uint32_t sent() const noexcept { return sent_; }          // own task 线程
  [[nodiscard]] std::uint32_t received() const noexcept { return received_; }  // UI 任务线程

 private:
  Framework* fw_ = nullptr;
  std::uint32_t sent_ = 0;
  std::uint32_t received_ = 0;
};

/// 示例 App 4：一次性后台任务（issue 15 的任务生命周期演示）。
///
/// 与 TickerApp 的唯一区别是 period_ms = 0：入口跑一轮 onBackgroundTick 就返回。
/// "入口返回 = 任务结束"—— 平台 trampoline 记 finished 并 park，框架在 step 的回收段
/// 归还槽位，所以同一个槽可以被反复创建 / 跑完 / 回收。系统用例（ui_tour）拿它演示
/// 完整一轮：boot 时按策略创建一次，运行期再显式创建一次。
///
/// 观测线程纪律与 TickerApp 相同：runs_ 只被 own task 写，UI 任务读它是观测性读取
/// （v1 接受，见 docs/common-pitfalls.md 的"跨任务观测"一条）。
class JobApp final : public App {
 public:
  JobApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "job"; }
  [[nodiscard]] const char* title() const override { return "任务"; }
  // 无图标：启动器取标题首字符"任"兜底（字库已含"任务"两字）。

  [[nodiscard]] AppSettings settings() const override {
    // own_task + period_ms = 0 = 跑完即结束的短命任务（栈 256 字，优先级 4）。
    return AppSettings{BackgroundPolicy::own_task, 0U, 256U, 4U};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onBackgroundTick(std::uint32_t now_ms) override;
  void onExit() override;

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t runs() const noexcept { return runs_; }  // own task 线程

 private:
  std::uint32_t runs_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_DEMO_APPS_H */
