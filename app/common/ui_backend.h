/**
 * App 层与界面生成代码之间的**唯一接缝**（ADR 0008 → 0011 的迁移开关）。
 *
 * 三个 App（launcher / clock / settings）只认这三个函数，不认 EEZ 还是 AiDoGUI：
 *   - ui_backend_attach_nav(fw)      onCreate：把 UI 的切屏接到框架
 *   - ui_backend_load_for_app(name)  onEnter/onResume：装载本 App 的屏
 *   - ui_backend_tick()              onForegroundTick：给 UI 的每帧机会（EEZ 要，AiDoGUI 不用）
 *   - ui_backend_bind_click(s, w, a) onCreate：备用路径，声明"点屏 s 里的控件 w → 切到 App a"
 *     （主路是设计器里给控件挂「调用函数」事件 → embark_ui_open_<App 名>，见 aidogui_ui_hooks.h）
 * 编译期由宏选后端：EMBARK_AIDOGUI_UI 优先（新），否则 EMBARK_EEZ_UI_BRIDGE（旧）。
 * 两个宏都没定义（真机当前状态）= 三句全是空实现，App 照常编过、不碰 UI。
 */
#ifndef EMBARK_APP_UI_BACKEND_H
#define EMBARK_APP_UI_BACKEND_H

#include <embark/log.h>

#if defined(EMBARK_AIDOGUI_UI)

#include "aidogui_ui_bridge.h"
#include "aidogui_ui_nav.h"

namespace embark::demo {

inline constexpr const char* kUiBackendName = "aidogui";

inline void ui_backend_attach_nav(Framework& framework) {
  aidogui_ui_nav_attach(framework);
}

inline void ui_backend_load_for_app(const char* app_name) {
  if (!aidogui_ui_bridge_load_screen_for_app(app_name)) {
    // 工程里还没有这个 App 的同名屏：只记日志（屏名约定：screen == App 名），
    // 不装载任何屏 —— 屏幕上留着上一个 App 的界面比留白更糟。
    ELOG_INFO("AiDoGUI 工程里还没有 App {} 的屏（约定：screen == App 名）", app_name);
  }
}

inline void ui_backend_tick() { aidogui_ui_bridge_tick(); }

inline void ui_backend_bind_click(const char* screen_name, const char* widget_name,
                                  const char* app_name) {
  if (!aidogui_ui_nav_bind_click(screen_name, widget_name, app_name)) {
    ELOG_ERROR("AiDoGUI 点击绑定登记失败（参数为空或表满）：屏 {} 的 {} → App {}", screen_name,
               widget_name, app_name);
  }
}

}  // namespace embark::demo

#elif defined(EMBARK_EEZ_UI_BRIDGE)

#include "eez_ui_bridge.h"
#include "eez_ui_nav.h"

namespace embark::demo {

inline constexpr const char* kUiBackendName = "eez";

inline void ui_backend_attach_nav(Framework& framework) {
  eez_ui_nav_attach(framework);
}

inline void ui_backend_load_for_app(const char* app_name) {
  if (!eez_ui_bridge_load_screen_for_app(app_name)) {
    eez_ui_bridge_load_current_screen();  // EEZ 引擎自己的当前页
  }
}

inline void ui_backend_tick() { eez_ui_bridge_tick(); }

inline void ui_backend_bind_click(const char* screen_name, const char* widget_name,
                                  const char* app_name) {
  // EEZ 时代的导航由 Flow 的 SetPage 动作驱动（eez_ui_nav 里的 replacePageHook 观察者），
  // 没有"点控件 → App"这条路；签名留在这里只为让 App 层代码两端同形。
  (void)screen_name;
  (void)widget_name;
  (void)app_name;
}

}  // namespace embark::demo

#else

namespace embark::demo {

inline constexpr const char* kUiBackendName = "none";

inline void ui_backend_attach_nav(Framework& framework) { (void)framework; }
inline void ui_backend_load_for_app(const char* app_name) { (void)app_name; }
inline void ui_backend_tick() {}

inline void ui_backend_bind_click(const char* screen_name, const char* widget_name,
                                  const char* app_name) {
  (void)screen_name;  // 没有 UI 后端（真机当前状态）：导航声明编译成空操作
  (void)widget_name;
  (void)app_name;
}

}  // namespace embark::demo

#endif

#endif /* EMBARK_APP_UI_BACKEND_H */
