/**
 * EEZ Demo App 实现（issue 19 / ADR 0008）：见 eez_demo_app.h 头注释。
 *
 * 生命周期纪律与 hello_app.cpp 一致（spec §5 / issues/06）。UI 全部来自
 * EEZ Studio 生成代码（app/eez_ui/），本 App 不手写任何 lv_xxx 调用。
 */
#include <embark/log.h>

#include "eez_demo_app.h"
#include "eez_ui_bridge.h"

namespace embark::demo {

void EezDemoApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate", name());
  eez_ui_bridge_init();
}

void EezDemoApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
  ++enters_;
  eez_ui_bridge_load_current_screen();
}

void EezDemoApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void EezDemoApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
  ++resumes_;
  eez_ui_bridge_load_current_screen();
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
