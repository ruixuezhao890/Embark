/**
 * EEZ 屏 → App 导航桥实现（issue 19 / ADR 0008）：见 eez_ui_nav.h 头注释。
 *
 * 观察者是在 UI 任务里被 EEZ 生成代码回调的（用户点按钮 → Flow SetPage → 桥的
 * replacePageHook  → 这里），所以 request_switch 与框架自己的钩子同线程，安全；
 * 切换的真正生效仍在框架的 step 边界（request_switch 只登记请求）。
 */
#include "eez_ui_nav.h"

#include <embark/log.h>

#include "eez_ui_bridge.h"
#include "eez_ui_screen_names.h"

namespace embark::demo {
namespace {

Framework* g_framework = nullptr;    // 当前接线的框架（同一时刻只有一个 UI 框架）
int g_switch_requests = 0;          // 观测：登记了几次切换请求
const char* g_last_screen = nullptr;  // 观测：最近一次屏名（指向屏表静态字符串）

void on_eez_screen_changed(const char* screen_name, void* user) {
  auto* framework = static_cast<Framework*>(user);
  if (framework == nullptr || screen_name == nullptr) {
    return;
  }
  g_last_screen = screen_name;

  char app_name[eez_app_name_max];
  const char* app = eez_ui_app_name_for_screen(screen_name, app_name, sizeof(app_name));
  if (app == nullptr) {
    return;  // 屏名异常（空串/过长）：当作不认识
  }
  if (!framework->apps().valid(framework->apps().find(app))) {
    ELOG_INFO("EEZ 屏 {} 没有同名 App（屏名约定：screen == App 名），不切前台", screen_name);
    return;
  }

  ++g_switch_requests;
  ELOG_INFO("EEZ 屏 {} → 请求切到 App {}", screen_name, app);
  (void)framework->request_switch(app);
}

}  // namespace

void eez_ui_nav_attach(Framework& framework) {
  if (g_framework == &framework) {
    return;  // 同一个框架重复 attach = no-op（主屏 App 与示例 App 都可能调）
  }
  g_framework = &framework;
  eez_ui_bridge_ensure_init();  // 幂等：EEZ 生成代码在这里启动（含接管切屏事件）
  eez_ui_bridge_set_screen_observer(&on_eez_screen_changed, &framework);
}

void eez_ui_nav_detach() {
  eez_ui_bridge_set_screen_observer(nullptr, nullptr);
  g_framework = nullptr;
}

int eez_ui_nav_switch_requests() { return g_switch_requests; }

const char* eez_ui_nav_last_screen() { return g_last_screen; }

}  // namespace embark::demo
