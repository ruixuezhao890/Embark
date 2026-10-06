/**
 * Embark · 启动器 App（issue 16、ADR 0006；界面现由 EEZ Studio 提供）
 *
 * 主屏不再是手绘的扇形槽位架：EEZ Studio 工程里有一屏就叫 "launcher"
 * （屏名约定 == App 名，子页 = <app名>_<编号>_sub，见 eez_ui_bridge.h / eez_ui_nav.h），
 * 本 App 只负责把那一屏挂上显示器，并让屏上的控件驱动框架切 App：
 *
 *   - onCreate        → eez_ui_nav_attach(fw)：确保 EEZ 生成代码已启动（幂等），
 *                       并装上屏切换观察者——EEZ 里 SetPage 到某屏 = 框架切到该屏
 *                       同名的 App（屏名约定）；
 *   - onEnter/onResume → eez_ui_bridge_load_screen_for_app("launcher")：加载 EEZ 里
 *                       同名屏（缺失时回退 Flow 当前页）；
 *   - onForegroundTick → eez_ui_bridge_tick()：每帧驱动 eez_flow_tick()（前台专属，
 *                       issue 19 / ADR 0008）。
 *
 * 历史：issue 16 的扇形槽位架（弧轨 + 拖动 + 弹簧 + 手算命中）已随
 * include/embark/launcher_geometry.h 与 tests/kernel/test_launcher_geometry.cpp 一起
 * 下线；启动能力改由 EEZ 屏上的控件经 Flow SetPage 驱动。启动器仍是 App 0——
* EEZ 屏按钮 SetPage 回 launcher 屏后，屏观察者 request_switch 把前台送回这里（导航壳已退役）。
 *
 * 零堆纪律：类内没有堆分配；界面对象都由 EEZ 生成代码持有（见 eez_ui_bridge.cpp）。
 */
#ifndef EMBARK_APP_LAUNCHER_APP_H
#define EMBARK_APP_LAUNCHER_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class LauncherApp final : public App {
 public:
  LauncherApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "launcher"; }
  [[nodiscard]] const char* title() const override { return "启动器"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_HOME; }
  // accent() 默认全局强调色（design_tokens::accent）——启动器不挑色。

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onExit() override;

  // --- 验收观测点（ui_demo.cpp 的验收模式断言用）----------------------------
  [[nodiscard]] unsigned enters() const { return enters_; }
  [[nodiscard]] unsigned resumes() const { return resumes_; }
  [[nodiscard]] unsigned foreground_ticks() const { return foreground_ticks_; }

 private:
  unsigned enters_ = 0;
  unsigned resumes_ = 0;
  unsigned foreground_ticks_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_LAUNCHER_APP_H */