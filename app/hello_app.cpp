/**
 * Embark 最简 App 实现（issues/12）：见 hello_app.h 头注释。
 *
 * 薄壳纪律与 demo_apps.cpp 完全一致（spec §5 / issues/06；界面提交给 EEZ，
 * 手绘 lv_scr_load 已退役）：
 *   - onCreate 不建屏（无手绘 = 没有 screen_）；
 *   - onEnter/onResume 只做一件事 —— 按屏名约定加载自己的 EEZ 屏
 *       （eez_ui_bridge_enter_app_screen：还没画同名屏 → 保持当前屏 + 告警）；
 *   - onForegroundTick 泵 eez_ui_bridge_tick（LVGL 一帧）；
 *   - 想离开前台时 request_switch() 请求，框架在循环边界执行切换。
 *
 * 编译开关 EMBARK_EEZ_UI_BRIDGE：未定义的平台（esp32 真机）把桥调用编译成 no-op。
 */
#include <embark/log.h>

#include "hello_app.h"

#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#endif

namespace embark::demo {

void HelloApp::onCreate(Framework& fw) {
  (void)fw;  // 薄壳不持有框架句柄（suspend：无后台、无消息、不切换）
  ELOG_INFO("App {} onCreate（最简薄壳：界面 = EEZ 屏 hello，Studio 画好即生效）", name());
}

void HelloApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void HelloApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void HelloApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void HelloApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void HelloApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_tick();
#endif
}

}  // namespace embark::demo
