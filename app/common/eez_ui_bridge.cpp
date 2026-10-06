/**
 * EEZ UI 薄桥实现（issue 19 / ADR 0008）：见 eez_ui_bridge.h 头注释。
 *
 * 生成代码的 C 侧入口是 extern "C" 的 ui_init/ui_tick（ui.h 已声明，直接调）；
 * 当前页来自生成 eez-flow.h 的 g_currentScreen（0 起，-1 = 未初始化），页对象
 * 表是 screens.h 的 objects——成员序即页序 {launcher, clock, ...}，所以
 * (lv_obj_t**)&objects 就是页表：[0]=desk、[1]=clock。Flow 切屏时生成版
 * replacePageHook 会自己 lv_scr_load_anim，但 App 回前台时显示器仍挂启动器屏，
 * 因此 load_current_screen 必须显式把当前页挂上显示器。
 *
 * 按 id/名字切屏走 eez_flow_set_screen（生成版会先 createScreen，再同步
 * g_currentScreen，最后 lv_scr_load_anim）：直接 lv_scr_load 的话 g_currentScreen
 * 不更新，ui_tick 里的 tick_screen(g_currentScreen) 会继续 tick 旧屏。
 */
#include "eez_ui_bridge.h"

#include <cstring>

#include <embark/log.h>
#include <embark_eez_screens.h>  // 构建期生成的屏表（见 cmake/embark_eez_screens.cmake）
#include <embark_eez_vars.h>     // 构建期生成的变量表（见 cmake/embark_eez_vars.cmake）

#include "eez_ui/src/ui/eez-flow.h"
#include "eez_ui/src/ui/screens.h"
#include "eez_ui/src/ui/ui.h"

namespace embark::demo {
namespace {

bool g_ui_ready = false;  // ui_init() 之后才允许切屏（否则 Flow 还没建屏）

// 生成版切页实现（ui_init 时装上）；桥先保存它，再把自己的实现装上去转发。
using ReplacePageFn = void (*)(int16_t page_id, uint32_t anim_type, uint32_t speed, uint32_t delay);
ReplacePageFn g_prev_replace_page = nullptr;

EezScreenObserver g_observer = nullptr;  // 屏切换观察者（nullptr = 没人听）
void* g_observer_user = nullptr;

// 桥主动切屏窗口：窗口里的页变化不回调观察者（App 装载自己界面时不该回灌成"切 App"）。
bool g_bridge_loading = false;

// 桥的 replacePageHook（装到 eez::flow::replacePageHook 上）：先让生成版真的把页面
// 切过去（建页 + g_currentScreen 同步 + lv_scr_load_anim），再把页号翻成屏名回调观察者。
// 生成代码没有这个回调点，所以只能在这里链一层。
//
// 动画照原样转发：EEZ 的 SetPage 可以带动画（实测 anim=9（FADE_IN）、speed=200ms）。
// 这里曾经把参数归一化成"立即切屏"，因为 LVGL v8.3.11 的 lv_scr_load_anim 有个空指针坑
//（third_party/lvgl/src/core/lv_disp.c:233-237）：上一次切屏带动画时 display 里会留着
// d->scr_to_load，新的切屏进入该分支后，旧代码先调 scr_load_internal()（它把
// d->scr_to_load 清成 NULL，见同文件 :484），再拿 d->scr_to_load 去 lv_obj_set_pos() → 崩
//（本题实测：ClockApp::onEnter 的立即 lv_scr_load 撞上 EEZ 的动画切页）。
// 现在 lvgl 子模块已从 v8.3.11 升到上游修复提交 b1284c3 —— "fix(screen): fix crash when
// starting two screen loads with animations (#5062)"：把 scr_load_internal() 挪到所有使用
// d->scr_to_load 的语句之后。所以动画可以原样保留（改动只在本仓库的子模块 pin 上，
// 没有本地改第三方源码）。
void bridge_replace_page(int16_t page_id, uint32_t anim_type, uint32_t speed, uint32_t delay) {
  if (anim_type != LV_SCR_LOAD_ANIM_NONE || speed != 0 || delay != 0) {
    ELOG_INFO("EEZ 切页 {}（anim={} speed={} delay={}）", static_cast<int>(page_id), anim_type,
              speed, delay);
  }
  if (g_prev_replace_page != nullptr) {
    g_prev_replace_page(page_id, anim_type, speed, delay);
  }
  if (g_observer == nullptr || g_bridge_loading) {
    return;
  }
  const char* screen_name = eez_ui_bridge_screen_name(static_cast<int>(page_id) - 1);
  if (screen_name != nullptr) {
    g_observer(screen_name, g_observer_user);
  }
}

// ASCII 大小写不敏感的名字比较：屏名约定小写，App 名也小写，这里只做容错。
bool same_name(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  for (; *a != '\0' && *b != '\0'; ++a, ++b) {
    const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
    const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
    if (ca != cb) {
      return false;
    }
  }
  return *a == '\0' && *b == '\0';
}

// 变量名 → 变量索引：查构建期变量表，命中回填 index 并返回 true。
// 索引来自 Studio 导出（vars.h 的 FlowGlobalVariables），所以名字与索引天然同源。
bool var_index_of(const char* var_name, int& index) {
  if (var_name == nullptr) {
    return false;
  }
  for (int i = 0; i < ::embark::demo::eez::kVarCount; ++i) {
    if (same_name(::embark::demo::eez::kVars[i].name, var_name)) {
      index = ::embark::demo::eez::kVars[i].index;
      return true;
    }
  }
  return false;
}

// 启动时把变量表打出来：Studio 里加/删变量不用手改 C++，但日志要能看出桥认到了几个。
void log_var_table() {
  ELOG_INFO("EEZ 变量：{} 个（Flow 全局变量 = UI 显示数据接口）",
            ::embark::demo::eez::kVarCount);
  for (int i = 0; i < ::embark::demo::eez::kVarCount; ++i) {
    ELOG_INFO("  [{}] {}", ::embark::demo::eez::kVars[i].index,
              ::embark::demo::eez::kVars[i].name);
  }
}

}  // namespace

void eez_ui_bridge_ensure_init() {
  if (g_ui_ready) {
    return;  // 幂等：主屏 App 与示例 App 都会调，谁先来谁做
  }
  ui_init();
  g_ui_ready = true;
  // ui_init 里生成版已经把自己装到 eez::flow::replacePageHook 上；这里接管它，
  // 转发链保证页面照常切，同时把"切到了哪一屏"暴露给观察者。
  // 注意必须写 ::eez::flow —— 本文件在 embark::demo 里，而屏表的命名空间正好叫
  // embark::demo::eez，不加前导 :: 会被解析成 embark::demo::eez::flow。
  g_prev_replace_page = ::eez::flow::replacePageHook;
  ::eez::flow::replacePageHook = &bridge_replace_page;
  ELOG_INFO("EEZ 生成代码已启动：{} 个屏（屏名约定 == App 名），已接管切屏事件",
            ::embark::demo::eez::kScreenCount);
  log_var_table();
}
void eez_ui_bridge_tick() {
  ui_tick();
}

void eez_ui_bridge_load_current_screen() {
  if (g_currentScreen < 0) {
    return;  // Flow 尚未初始化（理论上 onCreate 先于 onEnter，不会走到）。
  }
  lv_obj_t* screen = ((lv_obj_t**)&objects)[g_currentScreen];
  if (screen != nullptr) {
    lv_scr_load(screen);
  }
}

int eez_ui_bridge_current_screen() {
  return static_cast<int>(eez_flow_get_current_screen());
}

bool eez_ui_bridge_screen_created(const char* screen_name) {
  if (!g_ui_ready) {
    return false;  // Flow 未启动（ui_init() 之前对象表还是全 0）
  }
  const int screen_id = eez_ui_bridge_screen_id(screen_name);
  if (screen_id == kEezScreenNone) {
    return false;  // 屏名未命中
  }
  return eez_flow_is_screen_created(static_cast<int16_t>(screen_id));
}

int eez_ui_bridge_screen_count() {
  return ::embark::demo::eez::kScreenCount;
}

const char* eez_ui_bridge_screen_name(int index) {
  if (index < 0 || index >= ::embark::demo::eez::kScreenCount) {
    return nullptr;
  }
  return ::embark::demo::eez::kScreens[index].name;
}

int eez_ui_bridge_screen_id(const char* screen_name) {
  if (screen_name == nullptr) {
    return kEezScreenNone;
  }
  for (int i = 0; i < ::embark::demo::eez::kScreenCount; ++i) {
    if (same_name(::embark::demo::eez::kScreens[i].name, screen_name)) {
      return ::embark::demo::eez::kScreens[i].id;
    }
  }
  return kEezScreenNone;
}

bool eez_ui_bridge_load_screen(int screen_id) {
  if (!g_ui_ready || screen_id < 1) {
    return false;
  }
  const bool was_loading = g_bridge_loading;
  g_bridge_loading = true;  // App 装载自己的界面：别让这次切屏被自己观察到
  eez_flow_set_screen(static_cast<int16_t>(screen_id), LV_SCR_LOAD_ANIM_NONE, 0, 0);
  g_bridge_loading = was_loading;
  return eez_ui_bridge_current_screen() == screen_id;
}

void eez_ui_bridge_set_screen_observer(EezScreenObserver observer, void* user) {
  g_observer = observer;
  g_observer_user = user;
}

bool eez_ui_bridge_load_screen_by_name(const char* screen_name) {
  const int screen_id = eez_ui_bridge_screen_id(screen_name);
  return screen_id != kEezScreenNone && eez_ui_bridge_load_screen(screen_id);
}

bool eez_ui_bridge_load_screen_for_app(const char* app_name) {
  return eez_ui_bridge_load_screen_by_name(app_name);
}

bool eez_ui_bridge_enter_app_screen(const char* app_name) {
  if (eez_ui_bridge_load_screen_for_app(app_name)) {
    return true;
  }
  if (app_name != nullptr) {
    ELOG_WARN("App {} 还没有同名 EEZ 屏（屏名约定：screen == App 名）：保持当前屏", app_name);
  }
  eez_ui_bridge_load_current_screen();
  return false;
}

int eez_ui_bridge_var_count() {
  return ::embark::demo::eez::kVarCount;
}

const char* eez_ui_bridge_var_name(int index) {
  if (index < 0 || index >= ::embark::demo::eez::kVarCount) {
    return nullptr;
  }
  return ::embark::demo::eez::kVars[index].name;
}

int eez_ui_bridge_var_index(const char* var_name) {
  int index = -1;
  return var_index_of(var_name, index) ? index : kEezVarNone;
}

// 写变量：只有 ui_init() 之后才写得到全局存储（在那之前生成代码的 g_globalVariables
// 还没分配，setGlobalVariable 只会落到 blob 初值上）。名字不存在一律不写。
bool eez_ui_bridge_set_var_int(const char* var_name, std::int32_t value) {
  int index = -1;
  if (!g_ui_ready || !var_index_of(var_name, index)) {
    return false;
  }
  ::eez::flow::setGlobalVariable(static_cast<uint32_t>(index), ::eez::IntegerValue(value));
  return true;
}

bool eez_ui_bridge_set_var_float(const char* var_name, float value) {
  int index = -1;
  if (!g_ui_ready || !var_index_of(var_name, index)) {
    return false;
  }
  ::eez::flow::setGlobalVariable(static_cast<uint32_t>(index), ::eez::FloatValue(value));
  return true;
}

bool eez_ui_bridge_set_var_bool(const char* var_name, bool value) {
  int index = -1;
  if (!g_ui_ready || !var_index_of(var_name, index)) {
    return false;
  }
  ::eez::flow::setGlobalVariable(static_cast<uint32_t>(index), ::eez::BooleanValue(value));
  return true;
}

bool eez_ui_bridge_set_var_string(const char* var_name, const char* value) {
  int index = -1;
  if (!g_ui_ready || value == nullptr || !var_index_of(var_name, index)) {
    return false;
  }
  ::eez::flow::setGlobalVariable(static_cast<uint32_t>(index), ::eez::StringValue(value));
  return true;
}

bool eez_ui_bridge_get_var_int(const char* var_name, std::int32_t& out) {
  int index = -1;
  if (!g_ui_ready || !var_index_of(var_name, index)) {
    return false;
  }
  out = ::eez::flow::getGlobalVariable(static_cast<uint32_t>(index)).getInt32();
  return true;
}

bool eez_ui_bridge_get_var_float(const char* var_name, float& out) {
  int index = -1;
  if (!g_ui_ready || !var_index_of(var_name, index)) {
    return false;
  }
  out = ::eez::flow::getGlobalVariable(static_cast<uint32_t>(index)).getFloat();
  return true;
}

bool eez_ui_bridge_get_var_bool(const char* var_name, bool& out) {
  int index = -1;
  if (!g_ui_ready || !var_index_of(var_name, index)) {
    return false;
  }
  out = ::eez::flow::getGlobalVariable(static_cast<uint32_t>(index)).getBoolean();
  return true;
}

}  // namespace embark::demo
