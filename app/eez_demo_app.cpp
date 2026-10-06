/**
 * EEZ Demo App 实现（issue 19 / ADR 0008）：见 eez_demo_app.h 头注释。
 *
 * 生命周期纪律与 hello_app.cpp 一致（spec §5 / issues/06）。UI 全部来自
 * EEZ Studio 生成代码（app/eez_ui/），本 App 不手写任何 lv_xxx 调用。
 */
#include <embark/log.h>

#include "eez_demo_app.h"
#include "eez_ui_bridge.h"
#include "eez_ui_nav.h"

namespace embark::demo {
namespace {

// 屏名约定（用户口径）：EEZ 里 screen 名 == App 名，子页 = <app名>_<编号>_sub。
// 本 App 叫 eezdemo；EEZ 工程里还没有同名屏时 load_screen_for_app 返回 false，
// 于是回退到 Flow 当前页。反方向（EEZ 里切屏 → 切前台 App）由 eez_ui_nav 的
// 观察者负责，见 onCreate 里的 eez_ui_nav_attach。
void load_screen_for_app_name(const char* app_name) {
  if (!eez_ui_bridge_load_screen_for_app(app_name)) {
    eez_ui_bridge_load_current_screen();
  }
}

}  // namespace

void EezDemoApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate", name());
  // 谁当 EEZ UI 的宿主谁接线：确保生成代码已启动（幂等）+ 装上屏切换观察者，
  // 于是 EEZ 里 SetPage 到某屏 = 框架切到那个屏对应的 App（屏名约定 == App 名）。
  eez_ui_nav_attach(fw);
}

void EezDemoApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
  ++enters_;
  load_screen_for_app_name(name());
}

void EezDemoApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void EezDemoApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
  ++resumes_;
  load_screen_for_app_name(name());
}

void EezDemoApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++foreground_ticks_;
  eez_ui_bridge_tick();
}

void EezDemoApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

}  // namespace embark::demo
