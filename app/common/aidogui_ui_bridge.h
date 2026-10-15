/**
 * AiDoGUI UI 薄桥（ADR 0011）：把 AiDoGUI 生成代码接到 Embark App 契约。
 * 这是 `app/common/eez_ui_bridge.h` 的继任者（EEZ 版在迁移第二步退役）。
 *
 * 生成代码（app/aidogui_ui/export/，AiDoGUI 自己的导出布局）是 C，不直接认识 Embark；
 * 这一层是唯一适配点。
 *
 * 一、与 EEZ 生成代码的三个关键差别（决定了本桥的形状）
 *   1) **不调用生成的 aidogui_init()**：它一次性建全部屏、从不回收，且字体加载失败
 *      就直接 return（连屏都不建）。本框架的纪律是「切走即释放」（屏生命周期由
 *      App 的 onEnter/onLeave 决定），所以本桥自己承担 aidogui_init 里的三件事：
 *      注册 LV_EVENT_GET_COMP_CHILD、加载字体、按需调用
 *      ui_<屏id>_screen_init() / ui_<屏id>_screen_destroy()。
 *   2) **没有 tick**：EEZ 每帧刷变量到控件（ui_tick）；AiDoGUI 生成代码没有变量层，
 *      控件值由 App 直接写（aidogui_ui_bridge_widget 拿到 lv_obj_t* 后用 LVGL API）。
 *      aidogui_ui_bridge_tick() 保留成 no-op 只是为了 App 契约形状不变。
 *   3) **字体是外部文件**：字体在工程里选了 BIN 输出，生成代码写死
 *      lv_font_load("S:/fonts/<name>.bin")，'S' 盘由 platform/common/aidogui_res_fs
 *      挂载（宿主 = 仓库 app/aidogui_ui/export；真机 = SD 卡挂载点）。
 *      字体加载失败只记警告并降级（该字号退回 LVGL 内置字体），**不让整屏失效**。
 *
 * 二、屏（屏名约定不变：screen 名 == App 名；子页 <app名>_<编号>_sub）
 *   - aidogui_ui_bridge_screen_count() / _screen_name(i)：遍历生成代码里的屏表；
 *   - aidogui_ui_bridge_has_screen(name)：工程里有没有这屏；
 *   - aidogui_ui_bridge_load_screen(name) / _load_screen_for_app(app)：
 *     装载（先建新屏 + lv_scr_load，再销毁旧屏 —— 显示器上不会出现已删除对象）；
 *   - aidogui_ui_bridge_unload_current()：释放当前屏（显示器交给桥的空屏兜底：LVGL 8
 *     删掉当前屏后 disp->act_scr 会归 NULL，再装载时 lv_scr_load 会解引用 NULL 而崩，
 *     所以桥保证「显示器上永远有一张有效当前屏」——就是那张空屏，destroy 也不删）；
 *   - aidogui_ui_bridge_current_screen()：当前装载的屏名（未装载返回 nullptr）。
 *
 * 三、控件表（App 读写界面数据的接口，替代 EEZ 的 Flow 全局变量）
 *   构建期由 cmake/embark_aidogui_tables.cmake 在配置期解析 app/aidogui_ui/export 得到
 *   （构建目录 include/embark_aidogui_ui_tables.h，不手改、不入库）：
 *   - aidogui_ui_bridge_widget_count(screen) / _widget_name(screen, i)：遍历控件表；
 *   - aidogui_ui_bridge_widget(screen, widget_name)：按名拿 lv_obj_t*（屏未装载、
 *     控件名不存在一律返回 nullptr）。控件在 AiDoGUI 里改名只会让这里查不到
 *     （日志能看出来），不会像直接引 ui_<控件> 全局那样编译期静默断链。
 *   控件名 = AiDoGUI 里的控件名（如 L_Time、Card_Clock；主屏磁贴是运行时生成的，不在控件表里）。
 *
 * 四、屏切换事件（反向：AiDoGUI 里的动作 → Embark 能听到）
 *   AiDoGUI 的事件动作 CHANGE_SCREEN 会调生成代码里的 _ui_screen_change()（LVGL 切屏），
 *   本桥包装在那个出口上：aidogui_ui_bridge_set_screen_observer(observer, user) 装的
 *   观察者会收到目标屏名，再经 app/common/aidogui_ui_nav.h 解析成 App 名请求切前台。
 *   桥自己发起的 load_screen* **也**会回调（notify=true）：App 在 onEnter 里装载自己的屏时，
 *   观察者会收到同名屏，导航侧按"已经在前台"忽略。于是"每次屏装载都要做的事"（点击绑定、
 *   上滑返回手势）只需要挂在观察者这一处，不必区分屏是谁装载的。
 *
 * 头只 include <lvgl.h>：不把生成代码的头泄进 Embark App 编译面。
 */
#ifndef EMBARK_APP_AIDOGUI_UI_BRIDGE_H
#define EMBARK_APP_AIDOGUI_UI_BRIDGE_H

#include <lvgl.h>

namespace embark::demo {

/// 字体加载报告（验收探针：期望 total == loaded == 工程里用到的字体数）。
struct AidoguiFontReport {
  int total;   // 生成代码里声明的字体（字体文件 × 字号）
  int loaded;  // lv_font_load 成功的个数
};

/// 一次性初始化（幂等）：挂 'S' 盘、注册 LV_EVENT_GET_COMP_CHILD、加载字体。
/// 不建屏、不换屏（屏由 load_screen* 按需建）。显示未就绪时返回 false。
bool aidogui_ui_bridge_init();

/// 释放：卸载当前屏并释放字体（退出/测试用；之后可再 init）。
void aidogui_ui_bridge_destroy();

bool aidogui_ui_bridge_ready();

/// 字体报告（未 init 时两项都是 0）。
AidoguiFontReport aidogui_ui_bridge_font_report();

/// 按字号取已加载的字体：AiDoGUI 的字体 BIN 文件名以 "_<字号>.bin" 结尾
/// （如 ui_font_ui_heiti_14.bin），没有该字号或它加载失败时返回 nullptr
/// （调用方保持 LVGL 内置字体即可）。运行时新建的控件（启动器磁贴）要用设计器同款字体，
/// 只能走这里 —— 生成代码的 ui_font_* 符号不泄进 App 编译面（见本文件头）。
lv_font_t* aidogui_ui_bridge_font(int size);

// --- 屏表 -------------------------------------------------------------------
int aidogui_ui_bridge_screen_count();
const char* aidogui_ui_bridge_screen_name(int index);
bool aidogui_ui_bridge_has_screen(const char* screen_name);

/// 装载屏（未命中返回 false）。切走即释放：装载新屏时会销毁旧屏。
bool aidogui_ui_bridge_load_screen(const char* screen_name);
/// 按屏名 == App 名的约定装载该 App 的屏（没有同名屏返回 false）。
bool aidogui_ui_bridge_load_screen_for_app(const char* app_name);
/// 释放当前屏（没有屏时是 no-op）。验收探针用：切走即释放。
void aidogui_ui_bridge_unload_current();
/// 当前装载的屏名（未装载 nullptr；指针指向屏表里的静态字符串）。
const char* aidogui_ui_bridge_current_screen();
/// 该屏此刻是否已创建（= 根控件对象存在且指针非空）。验收探针。
bool aidogui_ui_bridge_screen_created(const char* screen_name);

// --- 控件表 -----------------------------------------------------------------
/// 该屏的控件个数（含当前未创建的：表来自生成代码，不随装载变化）。
int aidogui_ui_bridge_widget_count(const char* screen_name);
const char* aidogui_ui_bridge_widget_name(const char* screen_name, int index);
/// 按名拿控件指针（屏未装载 / 控件名不存在 / 控件已被删 → nullptr）。
lv_obj_t* aidogui_ui_bridge_widget(const char* screen_name, const char* widget_name);

// --- 屏切换观察者 -----------------------------------------------------------
using AidoguiScreenObserver = void (*)(const char* screen_name, void* user);
void aidogui_ui_bridge_set_screen_observer(AidoguiScreenObserver observer, void* user);

/// 兼容 App 契约的空实现（AiDoGUI 生成代码没有每帧刷控件的变量层）。
void aidogui_ui_bridge_tick();

}  // namespace embark::demo

#endif /* EMBARK_APP_AIDOGUI_UI_BRIDGE_H */
