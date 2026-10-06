/**
 * Embark · 启动器 App 实现（issue 16、ADR 0006；界面现由 EEZ Studio 提供）
 *
 * 见 launcher_app.h 头注释。本文件与 clock_app.cpp / settings_app.cpp 同形：不手写任何
 * 界面调用，只把 EEZ 生成代码里同名屏（“launcher”）挂上显示器，并把 EEZ 的切屏事件
 * 反向接到框架的 App 切换上（屏名约定 == App 名，解析在 eez_ui_screen_names.h）。
 *
 * 桥调用的 EMBARK_EEZ_UI_BRIDGE guard：宿主构建（app/CMakeLists.txt 定义）接 EEZ 生成
 * 代码；未定义的目标（esp32 真机，暂未接 EEZ）把桥调用编译成 no-op —— 本 App 照常
 * 可跑，真机接入 EEZ 后打开开关即可（esp32 的 EMBARK_APP_TABLE 已含 LauncherApp）。
 */
#include <embark/error.h>
#include <embark/log.h>

#include "launcher_app.h"

#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#include "eez_ui_nav.h"
#endif

namespace embark::demo {
namespace {

#if defined(EMBARK_EEZ_UI_BRIDGE)
// 屏名约定（用户口径）：EEZ 里 screen 名 == App 名，子页 = <app名>_<编号>_sub。
// EEZ 工程里还没有同名屏时 load_screen_for_app 返回 false，于是回退到 Flow 当前页。
// 反方向（EEZ 里切屏 → 切前台 App）由 eez_ui_nav 的观察者负责，见 onCreate。
void load_screen_for_app_name(const char* app_name) {
  if (!eez_ui_bridge_load_screen_for_app(app_name)) {
    eez_ui_bridge_load_current_screen();
  }
}
#endif

}  // namespace

void LauncherApp::onCreate(Framework& fw) {
  ELOG_INFO("App {} onCreate", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  // 谁当 EEZ UI 的宿主谁接线：确保生成代码已启动（幂等）+ 装上屏切换观察者，
  // 于是 EEZ 里 SetPage 到某屏 = 框架切到那个屏对应的 App（屏名约定 == App 名）。
  eez_ui_nav_attach(fw);
#endif
}

void LauncherApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_EEZ_UI_BRIDGE)
  load_screen_for_app_name(name());
#endif
}

void LauncherApp::onPause() {
  ELOG_INFO("App {} 退到后台（enter {}, resume {}）", name(), enters_, resumes_);
}

void LauncherApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_EEZ_UI_BRIDGE)
  load_screen_for_app_name(name());
#endif
}

void LauncherApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++foreground_ticks_;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_tick();
#endif
}

void LauncherApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

}  // namespace embark::demo
