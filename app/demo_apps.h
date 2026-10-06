/**
 * Embark 示例 App（issues/08，spec §14.1 的验收载体；issue 16 起带启动器元数据；
 *      手绘界面已全部退役 —— 界面统一由 EEZ Studio 生成代码提供，见 eez_ui_bridge.h）
 *
 * 四个 App 各演示一种后台策略，凑齐 spec §14.1 的“≥2 个 App、可切前台、后台 tick
 * 可观测”，并顺带示范 spec §16.4 推荐的 etl::state_chart：
 *   - LauncherApp（主屏/默认前台，后台策略 = suspend，见 launcher_app.h）：界面 =
 *       EEZ 屏 “launcher”；屏上按钮经 Flow SetPage 到某屏 = 启动那个 App（屏名约定：
 *       screen 名 == App 名，子页 <app名>_<编号>_sub —— 见 eez_ui_nav.h）。
 *   - ClockApp（后台策略 = tick，周期 100 ms）：闪烁时钟的逻辑演示 —— 内部状态用
 *       etl::state_chart（led on/off 两态，tick 事件来回切，on_entry 记日志）；每拍
 *       +1 计数；收 SettingsApp 发来的 BrightnessMessage 更新亮度档（消息驱动，App 间
 *       不互相 include、不碰对方任何符号）。界面在 EEZ 屏 “clock”（用户按约定在
 *       Studio 里画）；本 App 每拍把计数推进 Flow 全局变量 clock_tick_count —— 那
 *       就是 UI 显示数据的接口：变量在 Studio 声明并绑到控件后自动生效，C++ 零改动。
 *   - SettingsApp（后台策略 = suspend）：亮度档 0..3 循环 + 广播 BrightnessMessage。
 *       手绘的 “Level +1” 按钮已退役：逻辑入口 bump_level() 交给 EEZ Flow 动作/变量
 *       或无人值守验收调用（界面在 Studio 画好 settings 屏后由 EEZ 屏提供）。
 *   - TickerApp（后台策略 = own_task）：自己的 FreeRTOS 任务每 50 ms 发一条
 *       CrossTaskMessage 回 UI —— 证明“后台长活不阻塞 UI”与“消息经收件箱回 UI”
 *       （issue 07 的机制，原样迁移）。
 *   - JobApp（后台策略 = own_task，period_ms = 0）：一次性任务 —— 入口跑一轮
 *       onBackgroundTick 就返回；平台记 finished 并 park，框架在 step 的回收段归还槽位，
 *       于是“创建 → 跑完 → 回收 → 再创建”能反复走（issue 15 的任务生命周期）。
 *       它只挂在系统用例（platform/host/ui_tour.cpp）的注册表里。
 *   - EezDemoApp（EEZ Studio 生成 UI，issue 19 / ADR 0008）：见 eez_demo_app.h。
 *
 * App 元数据（issue 16）：title() 中文标题 / icon() LV_SYMBOL 码点（可选，字库已含）/
 * accent() 强调色（可选，默认全局强调色），随 EMBARK_APP_TABLE 编译期注册；
 * 界面不再由启动器消费（EEZ 屏静态画好），元数据保留给框架与后续界面使用。
 *
 * 薄壳纪律（手绘 UI 退役后统一，与 launcher_app.cpp / eez_demo_app.cpp 同形）：
 *   - App 不再建屏、不再 lv_scr_load；onCreate 只记账/启后台逻辑（ClockApp 顺带
 *       chart_.start()）/接线（谁当 EEZ 宿主谁 eez_ui_nav_attach，见 launcher_app）；
 *   - onEnter/onResume 只做一件事：按屏名约定加载自己的 EEZ 屏
 *       （eez_ui_bridge_enter_app_screen —— Studio 还没画同名屏时保持当前屏 + 告警，
 *       缺屏 App 策略 A：画好即自动生效）；
 *   - onForegroundTick 泵 eez_ui_bridge_tick（LVGL 一帧，tick_screen_* 刷新绑了变量
 *       的控件），值变化的 App（ClockApp 每拍）把数据显示推进 Flow 全局变量；
*   - 切换只经 fw.request_switch() 请求，框架在循环边界执行（回主屏由 EEZ 屏按钮
*       SetPage 回 launcher 屏承担，App 不自己画“返回”按钮 —— 导航壳 2026-10-06 退役）；
 *   - App 之间只走消息（SettingsApp publish → ClockApp onMessage）。
 *
 * 编译开关 EMBARK_EEZ_UI_BRIDGE：宿主构建（app/CMakeLists.txt 定义）接 EEZ 生成代码；
 * 未定义的目标（esp32 真机：platform/esp32/project/components/embark/CMakeLists.txt
 * 还没把 eez_ui_* 与生成代码编进去，见 Open objectives）把桥调用编译成 no-op ——
 * App 逻辑（状态机/消息/own_task）照常可跑，真机接入 EEZ 后打开开关即可。
 *
 * 换后端不动本目录：这里只依赖 embark 公开头、LVGL（仅头符号）、ETL 与 EEZ 薄桥，
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

/// 示例 App 之间的消息（App 间只走消息，不互相 include —— spec §7 / issue 08 验收）。
/// id 0x21 是 demo 私有区（框架保留 0xFE 给跨任务信封，勿撞）。
/// 注意：有基类（MessageT）就不是聚合了，必须给显式构造（ETL message 是平凡默认构造）。
struct BrightnessMessage : public MessageT<0x21> {
  constexpr BrightnessMessage(std::uint8_t level_value = 0) noexcept : level(level_value) {}
  std::uint8_t level = 0;  // 0..3 共四档
};

/// 示例 App 1：闪烁时钟（薄壳）。后台策略 = tick（100 ms）；内部状态 = etl::state_chart 示范。
/// 界面 = EEZ 屏 “clock”；每拍把计数推进 Flow 全局变量 clock_tick_count（UI 显示数据接口）。
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
  void onForegroundTick(std::uint32_t now_ms) override;
  void onMessage(const Message& msg) override;

  // --- 验收观测（本 App 无 own task，全部在 UI 任务里跑，非线程敏感）--------------
  [[nodiscard]] std::uint32_t ticks() const noexcept { return ticks_; }
  [[nodiscard]] std::uint32_t brightness() const noexcept { return level_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }
  [[nodiscard]] std::uint32_t foreground_ticks() const noexcept { return foreground_ticks_; }

 private:
  // 状态机（示范 §16.4）：led on/off 两态 + tick 事件来回切；on_entry 只记账/打日志
  // （视觉副作用已随手绘 UI 退役，改由 EEZ 屏绑定变量按值呈现）。
  enum class State : std::uint8_t { blinking_on = 0, blinking_off = 1 };
  enum class Event : std::uint8_t { tick = 0 };

  using Chart = etl::state_chart<ClockApp>;        // TParameter = void（无参事件）
  static const Chart::transition kTransitions[2];  // 定义在 demo_apps.cpp
  static const Chart::state kStates[2];            // 定义在 demo_apps.cpp
  Chart chart_;

  void on_tick() noexcept;             // 状态机 action：++ticks_ + 推进 clock_tick_count
  void enter_blinking_on() noexcept;   // on_entry：打日志
  void enter_blinking_off() noexcept;  // on_entry：打日志

  std::uint32_t ticks_ = 0;
  std::uint32_t level_ = 0;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
  std::uint32_t foreground_ticks_ = 0;
};

/// 示例 App 2：设置页（薄壳）。后台策略 = 默认（suspend）：纯前台交互，退后台完全不跑。
/// 手绘 “Level +1” 按钮已退役：亮度档逻辑入口 bump_level() 留给 EEZ Flow 动作/验收。
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
  void onForegroundTick(std::uint32_t now_ms) override;

  /// 亮度档逻辑入口（手绘按钮退役后，触发点交给 EEZ Flow 动作/变量或无人值守验收）：
  /// 档位 0..3 循环 +1，并广播 BrightnessMessage（ClockApp 的 onMessage 会收到）。
  void bump_level();

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t level() const noexcept { return level_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  Framework* fw_ = nullptr;
  std::uint32_t level_ = 0;   // 演示消息：本 App 也持一份副本（观察用）
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

/// 示例 App 3：后台重活演示（薄壳）。后台策略 = own_task（issue 07 机制原样迁移）。
/// 自己的 FreeRTOS 任务每 50 ms 发一条 CrossTaskMessage 回 UI。
/// 观测线程纪律：sent_ 只被 own task 写，received_ 只被 UI 任务写（单写者，
/// 不需要原子；验收时在 UI 任务里读 sent_ 是观测性读取，v1 接受）。
class TickerApp final : public App {
 public:
  TickerApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "ticker"; }
  [[nodiscard]] const char* title() const override { return "心跳"; }
  // 无图标：icon() 留空（界面在 EEZ 屏里静态画好，不再由启动器消费）。

  [[nodiscard]] AppSettings settings() const override {
    // own_task：50 ms 周期；栈 512 字 × 8 字节/字 = 4 KB；优先级 4（低于 UI 任务的 5）。
    // 4 KB 是 bring-up 实测值：一条含 uint16 字段的 ELOG（CrossTaskMessage::from_app）会走
    // efmt 的 stream 兜底（std::ostringstream），2 KB 栈装不下——真机上实测把 TCB 冲坏。
    return AppSettings{BackgroundPolicy::own_task, 50U, 512U, 4U};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onBackgroundTick(std::uint32_t now_ms) override;
  void onForegroundTick(std::uint32_t now_ms) override;
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

/// 示例 App 4：一次性后台任务（薄壳，issue 15 的任务生命周期演示）。
///
/// 与 TickerApp 的唯一区别是 period_ms = 0：入口跑一轮 onBackgroundTick 就返回。
/// “入口返回 = 任务结束”—— 平台 trampoline 记 finished 并 park，框架在 step 的回收段
/// 归还槽位，所以同一个槽可以被反复创建 / 跑完 / 回收。系统用例（ui_tour）拿它演示
/// 完整一轮：boot 时按策略创建一次，运行期再显式创建一次。
///
/// 观测线程纪律与 TickerApp 相同：runs_ 只被 own task 写，UI 任务读它是观测性读取
/// （v1 接受，见 docs/common-pitfalls.md 的“跨任务观测”一条）。
class JobApp final : public App {
 public:
  JobApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "job"; }
  [[nodiscard]] const char* title() const override { return "任务"; }
  // 无图标：icon() 留空（同 TickerApp；界面在 EEZ 屏里静态画好，不再由启动器消费）。

  [[nodiscard]] AppSettings settings() const override {
    // own_task + period_ms = 0 = 跑完即结束的短命任务（栈 256 字，优先级 4）。
    return AppSettings{BackgroundPolicy::own_task, 0U, 256U, 4U};
  }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onBackgroundTick(std::uint32_t now_ms) override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onExit() override;

  // --- 验收观测 --------------------------------------------------------------
  [[nodiscard]] std::uint32_t runs() const noexcept { return runs_; }  // own task 线程

 private:
  std::uint32_t runs_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_DEMO_APPS_H */
