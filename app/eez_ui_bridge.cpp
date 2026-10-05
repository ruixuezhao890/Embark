/**
 * EEZ UI 薄桥实现（issue 19 / ADR 0008）：见 eez_ui_bridge.h 头注释。
 *
 * 生成代码的 C 侧入口是 extern "C" 的 ui_init/ui_tick；当前屏与 screen 对象表
 * 来自 <eez/flow/lvgl_api.h>（g_currentScreen，0 = 主屏、1 = HOME）与
 * screens.h 的 objects（main/home）。counter 是 vars.cpp 的 C++ 全局
 * （非 extern "C"），直接 extern 引用。
 */
#include "eez_ui_bridge.h"

#include "eez_ui/screens.h"
#include "eez_ui/ui.h"

#include <eez/flow/lvgl_api.h>

extern "C" void ui_init();
extern "C" void ui_tick();

// vars.cpp 的全局变量（action_inc/dec_counter 的落脚点）。
extern int32_t counter;

namespace embark::demo {

void eez_ui_bridge_init() {
  ui_init();
}

void eez_ui_bridge_tick() {
  ui_tick();
}

void eez_ui_bridge_load_current_screen() {
  // Flow 切屏（eez_flow_set_screen）只更新 g_currentScreen 并换"活动屏"标记，
  // 真正把屏幕挂上显示器的 lv_scr_load 归 Embark（App 自决界面生命周期，spec §5）。
  lv_obj_t* screen = (g_currentScreen >= 1) ? objects.home : objects.main;
  lv_scr_load(screen);
}

int eez_ui_bridge_counter() {
  return counter;
}

}  // namespace embark::demo
