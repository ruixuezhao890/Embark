/**
 * EEZ Demo App（issue 19 / ADR 0008）：EEZ Studio + Flow 在 Embark 里的第一个 App。
 *
 * 整个 App 只有 4 行与 EEZ 相关（见 eez_ui_bridge.h）：
 *   - onCreate  → eez_ui_bridge_init()：Flow 启动，创建主屏/HOME 屏；
 *   - onEnter / onResume → eez_ui_bridge_load_current_screen()：把 Flow 当前屏
 *     挂上显示器（载屏归 Embark，spec §5 生命周期自决）；
 *   - onForegroundTick → eez_ui_bridge_tick()：每帧驱动 eez_flow_tick()
 *     （issue 19 / ADR 0008 新钩子，前台专属）。
 *
 * 其余与手写 App 完全一致：title/icon 是启动器元数据；enters_/resumes_/
 * foreground_ticks_ 是宿主验收（ui_demo.cpp --eez）的观测点。
 */
#ifndef EMBARK_APP_EEZ_DEMO_APP_H
#define EMBARK_APP_EEZ_DEMO_APP_H

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class EezDemoApp final : public App {
 public:
  EezDemoApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "eezdemo"; }
  [[nodiscard]] const char* title() const override { return "EEZ"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_HOME; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onExit() override;

  // --- 验收观测点（ui_demo.cpp --eez 断言用）--------------------------------
  [[nodiscard]] unsigned enters() const { return enters_; }
  [[nodiscard]] unsigned resumes() const { return resumes_; }
  [[nodiscard]] unsigned foreground_ticks() const { return foreground_ticks_; }

 private:
  Framework* fw_ = nullptr;
  unsigned enters_ = 0;
  unsigned resumes_ = 0;
  unsigned foreground_ticks_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_EEZ_DEMO_APP_H */
