/**
 * Embark · 时钟 App（薄壳）。后台策略 = tick（100 ms）；内部状态用 etl::state_chart 示范
 * （led on/off 两态，tick 事件来回切，on_entry 记日志）。
 *
 * 界面 = EEZ 屏 “clock”（屏名约定：screen 名 == App 名，子页 <app名>_<编号>_sub）。
 * 本 App 每拍把计数推进 Flow 全局变量 clock_tick_count —— 那是 UI 显示数据的接口：
 * 变量在 Studio 声明并绑到控件后自动生效，C++ 零改动（见 eez_ui_bridge.h）。
 *
 * 消息：收 SettingsApp 广播的 BrightnessMessage（id 0x21，定义在 app/common/app_messages.h）
 * 更新亮度档 —— 消息驱动，App 间不互相 include、不碰对方任何符号（spec §7）。
 *
 * 薄壳纪律（与 launcher_app / settings_app 完全一致，2026-10-06 目录化后各 App 独立成目录）：
 *   - App 不建屏、不 lv_scr_load；onCreate 只记账/启后台逻辑（本 App 顺带 chart_.start()）；
 *   - onEnter/onResume 只做一件事：按屏名约定加载自己的 EEZ 屏
 *       （eez_ui_bridge_enter_app_screen —— Studio 还没画同名屏时保持当前屏 + 告警，
 *       缺屏 App 策略 A：画好即自动生效）；
 *   - onForegroundTick 泵 eez_ui_bridge_tick（LVGL 一帧，刷新绑了变量的控件），
 *       值变化（每拍）把数据显示推进 Flow 全局变量；
 *   - 切换只经 fw.request_switch() 请求；回主屏由 EEZ 屏按钮 SetPage 回 launcher 屏承担
 *       （导航壳 2026-10-06 退役）；
 *   - App 之间只走消息（SettingsApp publish → 本 App onMessage）。
 *
 * 编译开关 EMBARK_EEZ_UI_BRIDGE：宿主构建（app/CMakeLists.txt 定义）接 EEZ 生成代码；
 * 未定义的目标（esp32 真机）把桥调用编译成 no-op —— App 逻辑（状态机/消息）照常可跑，
 * 真机接入 EEZ 后打开开关即可。
 *
 * 换后端不动本目录：这里只依赖 embark 公开头、LVGL（仅头符号）、ETL 与 EEZ 薄桥，
 * 没有任何 platform/ 后端头（spec §14.5 验收项）。
 */
#ifndef EMBARK_APP_CLOCK_APP_H
#define EMBARK_APP_CLOCK_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>
#include <middleware/etl/state_chart.h>

namespace embark::demo {

/// 示例 App：闪烁时钟（薄壳）。后台策略 = tick（100 ms）；界面 = EEZ 屏 “clock”。
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
  // （视觉副作用已退役，改由 EEZ 屏绑定变量按值呈现）。
  enum class State : std::uint8_t { blinking_on = 0, blinking_off = 1 };
  enum class Event : std::uint8_t { tick = 0 };

  using Chart = etl::state_chart<ClockApp>;        // TParameter = void（无参事件）
  static const Chart::transition kTransitions[2];  // 定义在 clock_app.cpp
  static const Chart::state kStates[2];            // 定义在 clock_app.cpp
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

}  // namespace embark::demo

#endif /* EMBARK_APP_CLOCK_APP_H */
