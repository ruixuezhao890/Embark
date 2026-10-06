/**
 * EEZ UI 薄桥（issue 19 / ADR 0008）：把 EEZ 生成代码接到 Embark App 契约。
 *
 * 生成代码（app/eez_ui/src/ui/，EEZ Studio 每次导出可增删文件）是 C，
 * 不直接认识 Embark；这一层是唯一的适配点，分两组能力：
 *
 * 一、把 UI 跑起来（四句话）
 *   - eez_ui_bridge_init()      onCreate 里调一次 = ui_init()（Flow 启动，建屏）；
 *   - eez_ui_bridge_tick()      onForegroundTick 每帧一次 = ui_tick()；
 *   - eez_ui_bridge_load_current_screen()  onEnter/onResume = 把 Flow 当前页
 *     lv_scr_load 上来（App 从后台回前台时显示器还挂着启动器屏）；
 *   - eez_ui_bridge_current_screen()  观测桥（验收用）= Flow 当前页（1 起；
 *     0 = 尚未初始化）。
 *
 * 二、屏名 → App 的映射（屏名约定：screen 名 == App 名；子页 <app名>_<编号>_sub）
 *   屏表由构建期从生成代码解析（cmake/embark_eez_screens.cmake → 构建目录的
 *   embark_eez_screens.h），所以你在 EEZ Studio 里按约定增删屏，这里不用手改：
 *   - eez_ui_bridge_screen_count() / _screen_name(i)：遍历屏表；
 *   - eez_ui_bridge_screen_id(name)：屏名 → 屏 id（未命中返回 kEezScreenNone）；
 *   - eez_ui_bridge_load_screen(id) / _by_name(name)：切到指定屏（走 Flow 的
 *     eez_flow_set_screen，会同步 g_currentScreen，tick 不会跑错屏）；
 *   - eez_ui_bridge_load_screen_for_app(app_name)：按约定加载该 App 的屏；
 *     工程里还没有同名屏时返回 false，调用方回退 load_current_screen。
 *
 * 三、屏切换事件（反向：EEZ 里的操作 → Embark 能听到）
 *   生成代码把"切到某页"统一收敛到 eez::flow::replacePageHook 这个函数指针上
 *   （EEZ 的 SetPage 动作与 eez_flow_set_screen 都走它，见 eez-flow.cpp:6624），
 *   桥在启动时保存生成版指针再换成自己的：先转发给生成版（页面真的切过去、
 *   g_currentScreen 同步），再按屏表把页号翻成屏名回调观察者：
 *   - eez_ui_bridge_set_screen_observer(observer, user)：装/卸观察者（nullptr 卸掉）；
 *     桥自己发起的 load_screen* 不回调（那是 App 装载自己界面，不该回灌成切 App）。
 *   "屏名 → App" 的完整接线在 app/eez_ui_nav.h 的 eez_ui_nav_attach()。
 *
 * 四、Flow 全局变量（UI 显示数据的接口）
 *   EEZ Studio 里声明的 Flow 全局变量就是界面数据的来源：控件在 Studio 里绑到变量，
 *   生成代码每帧 tick_screen_*() 把变量值刷进控件。App 侧只按名字读写，变量索引由
 *   构建期变量表解析（cmake/embark_eez_vars.cmake → 构建目录的 embark_eez_vars.h）；
 *   变量命名约定沿用屏名约定：<app名>_<字段>，例如 launcher_tap_count、clock_hour。
 *   - eez_ui_bridge_var_count() / _var_name(i)：遍历变量表（面板/日志用）；
 *   - eez_ui_bridge_var_index(name)：变量名 → 索引（未命中返回 kEezVarNone）；
 *   - eez_ui_bridge_set_var_int/_float/_bool/_string(name, v)：写变量；
 *   - eez_ui_bridge_get_var_int/_float/_bool(name, out)：读变量。
 *   变量名不存在、UI 还没 init（ui_init() 之前）一律返回 false 且无副作用 —— 必须
 *   等 ui_init() 之后才能写，生成代码那时才分配全局变量的存储（否则只改到 blob 初值）。
 *
 * 头只 include <lvgl.h>：不把生成代码的头泄进 Embark App 编译面。
 */
#ifndef EMBARK_APP_EEZ_UI_BRIDGE_H
#define EMBARK_APP_EEZ_UI_BRIDGE_H

#include <cstdint>

#include <lvgl.h>

namespace embark::demo {

// 屏表：屏名 → 屏 id（id 从 1 起，0/-1 = 无效）
inline constexpr int kEezScreenNone = -1;

// 变量表：变量名 → 变量索引（索引是 Studio 里声明的顺序，0 起；-1 = 无效）
inline constexpr int kEezVarNone = -1;

void eez_ui_bridge_init();
/// 幂等版 init：已经起来就是 no-op。谁先启动都能安全调用（主屏 App、示例 App 都调它）。
void eez_ui_bridge_ensure_init();
void eez_ui_bridge_tick();
void eez_ui_bridge_load_current_screen();
int eez_ui_bridge_current_screen();

/// 只读探针：某屏现在是否已创建（生成代码 objects 表里非空）。UI 未启动 / 屏名未命中
/// 返回 false。EEZ 工程开了「Screens lifetime support」后，非启动屏离开即被生成代码
/// 回收（objects.* 置 0、Flow 状态释放），探针供验收用例断言「切走即释放，回来即重建」。
bool eez_ui_bridge_screen_created(const char* screen_name);

int eez_ui_bridge_screen_count();
const char* eez_ui_bridge_screen_name(int index);  // 越界返回 nullptr
int eez_ui_bridge_screen_id(const char* screen_name);

bool eez_ui_bridge_load_screen(int screen_id);
bool eez_ui_bridge_load_screen_by_name(const char* screen_name);
bool eez_ui_bridge_load_screen_for_app(const char* app_name);

/// onEnter/onResume 的标准动作：加载同名屏（= load_screen_for_app）；这个 App 还没在
/// Studio 里画屏时保持当前屏并打一条告警 —— 手绘界面退役后，缺屏的 App 也不会把
/// 显示器留成一片空白。返回 true = 真的切到了自己的屏。
bool eez_ui_bridge_enter_app_screen(const char* app_name);

// Flow 全局变量（UI 显示数据的接口）：按名读写，索引由构建期变量表解析。
int eez_ui_bridge_var_count();
const char* eez_ui_bridge_var_name(int index);  // 越界返回 nullptr
int eez_ui_bridge_var_index(const char* var_name);  // 未命中返回 kEezVarNone

bool eez_ui_bridge_set_var_int(const char* var_name, std::int32_t value);
bool eez_ui_bridge_set_var_float(const char* var_name, float value);
bool eez_ui_bridge_set_var_bool(const char* var_name, bool value);
bool eez_ui_bridge_set_var_string(const char* var_name, const char* value);

bool eez_ui_bridge_get_var_int(const char* var_name, std::int32_t& out);
bool eez_ui_bridge_get_var_float(const char* var_name, float& out);
bool eez_ui_bridge_get_var_bool(const char* var_name, bool& out);

/// 屏切换观察者：EEZ 里一次切屏落定后回调。screen_name 非空（页号越界不回调）；
/// user 是注册时原样透传的指针。桥自己发起的 load_screen* 不回调。
using EezScreenObserver = void (*)(const char* screen_name, void* user);
void eez_ui_bridge_set_screen_observer(EezScreenObserver observer, void* user);

}  // namespace embark::demo

#endif /* EMBARK_APP_EEZ_UI_BRIDGE_H */
