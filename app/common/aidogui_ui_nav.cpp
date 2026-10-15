/**
 * AiDoGUI 屏 → App 导航桥实现（ADR 0011）：见 aidogui_ui_nav.h 头注释。
 *
 * 观察者是在 UI 任务里被调用的（用户点按钮 → CHANGE_SCREEN → _ui_screen_change →
 * 本桥的包装 → 这里），与框架自己的钩子同线程，所以 request_switch 安全；
 * 切换的真正生效仍在框架的 step 边界（request_switch 只登记请求）。
 */
#include "aidogui_ui_nav.h"

#include <cstring>

#include <embark/log.h>

#include "aidogui_ui_bridge.h"
#include "aidogui_ui_hooks.h"     // 设计器「调用函数」事件的 C 入口声明（X 宏表）
#include "eez_ui_screen_names.h"  // 屏名→App 名的纯解析（约定与 EEZ 相同，第二步改名）

namespace embark::demo {
namespace {

Framework* g_framework = nullptr;  // 当前接线的框架（同一时刻只有一个 UI 框架）
int g_switch_requests = 0;         // 观测：登记了几次切换请求
const char* g_last_screen = nullptr;  // 观测：最近一次屏名（指向屏表静态字符串）

// --- 点击绑定（屏幕上的控件 → App）-----------------------------------------
// 备用路径：正常情况下导航由设计器里的「调用函数」事件驱动（见 aidogui_ui_hooks.h）。
// 表满时 bind 返回 false，不静默丢。
constexpr int kMaxClickBindings = 8;

// --- 上滑返回 -----------------------------------------------------------------
// 主屏 = 注册表第 0 个 App 的屏（App 0 就是启动器）。非主屏的屏根挂 LV_EVENT_GESTURE，
// LV_DIR_TOP → framework.request_home()。挂在屏根上而不是逐个控件上：屏是每次装载新建的，
// 观察者一定在"新屏刚建好"时被调用，挂一次就覆盖整屏。
int g_swipe_back_hits = 0;  // 观测：命中次数

struct ClickBinding {
  const char* screen;  // AiDoGUI 屏名（== App 名，约定同 EEZ）
  const char* widget;  // 控件名（设计器里的 name，如 Btn_Back / Card_Clock；主屏磁贴是运行时建的，不走这张表）
  const char* app;     // 目标 App 名
};

ClickBinding g_bindings[kMaxClickBindings] = {};
int g_binding_count = 0;
int g_click_hits = 0;  // 观测：命中次数

void on_widget_clicked(lv_event_t* event) {
  const auto* binding = static_cast<const ClickBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || binding->app == nullptr) {
    return;
  }
  ELOG_INFO("AiDoGUI 点击：屏 {} 的 {} → App {}", binding->screen, binding->widget, binding->app);
  (void)aidogui_ui_nav_request_app(binding->app);  // 与设计器事件同一条路
}

/// 屏装载后重挂点击回调。控件随屏 destroy 一起消失（LVGL 删对象时会摘掉它的事件链），
/// 所以每次装载都要 add 一遍；同一屏重复装载会被桥挡掉（g_current == entry），不会重复挂。
void bind_clicks_for(const char* screen_name) {
  for (int i = 0; i < g_binding_count; ++i) {
    const ClickBinding& binding = g_bindings[i];
    if (std::strcmp(binding.screen, screen_name) != 0) {
      continue;
    }
    lv_obj_t* widget = aidogui_ui_bridge_widget(screen_name, binding.widget);
    if (widget == nullptr) {
      ELOG_INFO("AiDoGUI 点击绑定：屏 {} 里没有控件 {}（设计器里改名了？）", screen_name,
                binding.widget);
      continue;
    }
    lv_obj_add_flag(widget, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(widget, &on_widget_clicked, LV_EVENT_CLICKED, &g_bindings[i]);
  }
}

/// 手势要能冒泡到屏根：LVGL 把 LV_EVENT_GESTURE 发给指针下的对象，然后沿父链往上冒，
/// 但只在带 LV_OBJ_FLAG_GESTURE_BUBBLE 的对象之间传（lv_indev.c 的 indev_gesture）。
/// 生成代码建的控件不带这个标志，所以子树补一遍。**屏根除外**：见 attach_swipe_back。
void add_gesture_bubble(lv_obj_t* obj) {
  if (obj == nullptr) {
    return;
  }
  lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
  const std::uint32_t children = lv_obj_get_child_cnt(obj);
  for (std::uint32_t i = 0; i < children; ++i) {
    add_gesture_bubble(lv_obj_get_child(obj, static_cast<int32_t>(i)));
  }
}

/// 屏根上的手势回调：上滑 = 回主屏。非上滑方向（左右滑翻页、下滑）一概不管。
void on_screen_gesture(lv_event_t* event) {
  auto* framework = static_cast<Framework*>(lv_event_get_user_data(event));
  lv_indev_t* indev = lv_indev_get_act();
  if (framework == nullptr || indev == nullptr) {
    return;
  }
  if (lv_indev_get_gesture_dir(indev) != LV_DIR_TOP) {
    return;
  }
  ++g_swipe_back_hits;
  ELOG_INFO("AiDoGUI 屏上滑：回主屏（第 {} 次）", g_swipe_back_hits);
  (void)framework->request_home();
}

/// 屏装载后给屏根挂手势（主屏跳过）。重复挂同一屏根不会发生：桥装载新屏时会先销毁旧屏，
/// 每次观察到的都是新对象；同屏重复装载被桥的 g_current == entry 挡掉。
void attach_swipe_back(const char* screen_name, Framework& framework) {
  const App* home = framework.apps().at(0);
  if (home != nullptr && home->name() != nullptr && std::strcmp(home->name(), screen_name) == 0) {
    return;  // 主屏：上滑不响应（回主屏 = 已经在主屏）
  }
  lv_obj_t* root = lv_scr_act();
  if (root == nullptr) {
    return;
  }
  // 屏根要能成为"指针下的对象"才谈得上派发：LVGL 的 lv_indev_search_obj() 只认
  // lv_obj_hit_test() 通过的控件，而它第一句就要求 CLICKABLE（lv_obj_pos.c:955）。
  // 生成代码给容器清了 CLICKABLE（屏根的 CLICKABLE 是 LVGL 默认，lv_obj.c:436），这里
  // 再显式加一次：将来设计器把屏根也清了，上滑不至于在空白处失灵。
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  // 冒泡标志**只给子树，屏根自己不带**：indev_gesture() 是从指针下的对象沿父链
  // "跳过带标志的对象"、落到第一个**没有**标志的对象上派发（lv_indev.c:1126-1133）；
  // 屏根也带标志时这条链一路走到 NULL，事件直接不发（手就在屏上滑，什么都没发生）。
  const std::uint32_t child_count = lv_obj_get_child_cnt(root);
  for (std::uint32_t i = 0; i < child_count; ++i) {
    add_gesture_bubble(lv_obj_get_child(root, static_cast<int32_t>(i)));
  }
  lv_obj_add_event_cb(root, &on_screen_gesture, LV_EVENT_GESTURE, &framework);
}

void on_aidogui_screen_changed(const char* screen_name, void* user) {
  auto* framework = static_cast<Framework*>(user);
  if (framework == nullptr || screen_name == nullptr) {
    return;
  }
  g_last_screen = screen_name;
  bind_clicks_for(screen_name);                 // 控件刚建好：把点击绑定挂回去
  attach_swipe_back(screen_name, *framework);    // 同上：手势也挂在新屏上

  char app_name[eez_app_name_max];
  const char* app = eez_ui_app_name_for_screen(screen_name, app_name, sizeof(app_name));
  if (app == nullptr) {
    return;  // 屏名异常（空串/过长）：当作不认识
  }
  const AppId target = framework->apps().find(app);
  if (!framework->apps().valid(target)) {
    ELOG_INFO("AiDoGUI 屏 {} 没有同名 App（屏名约定：screen == App 名），不切前台", screen_name);
    return;
  }
  if (target == framework->foreground()) {
    return;  // 已经在前台（App 在 onEnter/onResume 里装载自己的屏）：只重挂点击绑定
  }

  ++g_switch_requests;
  ELOG_INFO("AiDoGUI 屏 {} → 请求切到 App {}", screen_name, app);
  (void)framework->request_switch(app);
}

}  // namespace

int aidogui_ui_nav_swipe_back_hits() { return g_swipe_back_hits; }

void aidogui_ui_nav_attach(Framework& framework) {
  if (g_framework == &framework) {
    return;  // 同一个框架重复 attach = no-op
  }
  g_framework = &framework;
  aidogui_ui_bridge_init();  // 幂等：字体与资源盘在这里就位
  aidogui_ui_bridge_set_screen_observer(&on_aidogui_screen_changed, &framework);
}

void aidogui_ui_nav_detach() {
  aidogui_ui_bridge_set_screen_observer(nullptr, nullptr);
  g_framework = nullptr;
}

int aidogui_ui_nav_switch_requests() {
  return g_switch_requests;
}

const char* aidogui_ui_nav_last_screen() {
  return g_last_screen;
}

bool aidogui_ui_nav_request_app(const char* app_name) {
  if (g_framework == nullptr || app_name == nullptr || app_name[0] == '\0') {
    return false;
  }
  const AppId target = g_framework->apps().find(app_name);
  if (!g_framework->apps().valid(target)) {
    // 屏名/App 名约定（screen == App 名）在 aidogui_ui_screen_names.h；这里只认 App 名。
    ELOG_ERROR("AiDoGUI 导航：没有 App {}（设计器里的函数名写错了？）", app_name);
    return false;
  }
  if (target == g_framework->foreground()) {
    return true;  // 已经在前台：不重复登记（框架对同 App 请求本来就是 no-op）
  }
  ++g_click_hits;
  ELOG_INFO("AiDoGUI 导航：请求切到 App {}", app_name);
  return g_framework->request_switch(app_name) == Error::none;  // 只登记：框架在 step 边界执行
}

bool aidogui_ui_nav_bind_click(const char* screen_name, const char* widget_name,
                               const char* app_name) {
  if (screen_name == nullptr || widget_name == nullptr || app_name == nullptr ||
      screen_name[0] == '\0' || widget_name[0] == '\0' || app_name[0] == '\0') {
    return false;
  }
  for (int i = 0; i < g_binding_count; ++i) {
    if (std::strcmp(g_bindings[i].screen, screen_name) == 0 &&
        std::strcmp(g_bindings[i].widget, widget_name) == 0) {
      g_bindings[i].app = app_name;  // 同一控件重复声明：后写的目标生效
      return true;
    }
  }
  if (g_binding_count >= kMaxClickBindings) {
    return false;
  }
  g_bindings[g_binding_count++] = ClickBinding{screen_name, widget_name, app_name};
  ELOG_INFO("AiDoGUI 点击绑定：屏 {} 的 {} → App {}", screen_name, widget_name, app_name);
  return true;
}

int aidogui_ui_nav_click_bindings() {
  return g_binding_count;
}

int aidogui_ui_nav_click_hits() {
  return g_click_hits;
}

}  // namespace embark::demo

// --- 设计器「调用函数」事件的 C 入口（ADR 0011 决策⑦）----------------------
// 生成代码里出现的是裸调用（embark_ui_open_clock();），原型见
// app/common/aidogui_ui_hooks.h —— 那个头被 -include 强制注入到生成 C 文件。
extern "C" {

#define EMBARK_AIDOGUI_HOOK_DEFINE(app)                   \
  void embark_ui_open_##app(void) {                       \
    (void)embark::demo::aidogui_ui_nav_request_app(#app);  \
  }

EMBARK_AIDOGUI_HOOK_APPS(EMBARK_AIDOGUI_HOOK_DEFINE)
#undef EMBARK_AIDOGUI_HOOK_DEFINE

}  // extern "C"
