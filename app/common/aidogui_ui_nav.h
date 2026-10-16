/**
 * AiDoGUI 屏 → App 的导航桥（ADR 0011）：把"AiDoGUI 里切屏"变成"框架切前台 App"。
 * 这是 `app/common/eez_ui_nav.h` 的继任者，签名刻意一一对应（迁移期两边并存）。
 *
 * 分工：
 *   - app/common/aidogui_ui_bridge.h 只管 UI 生成代码（启动/屏表/控件表/切屏/观察者）；
 *   - app/common/eez_ui_screen_names.h 管屏名→App 名的纯解析（可单测；约定同 EEZ：
 *     屏名 == App 名，子页 <app名>_<编号>_sub。第二步改名成 ui_screen_names.h）；
 *   - 本文件把两边接起来：装观察者 → 屏名解析成 App 名 → framework.request_switch。
 *
 * 屏幕→App 的导航有三条路：
 *   - 观察者（本文件）：桥装载了某个屏 → 屏名解析成 App 名 → 有同名 App 就 request_switch。
 *     没有同名 App 的屏（还没实现/纯展示页）只记一条日志，不切前台。
 *   - 设计器事件（主路）：控件上挂「调用函数」动作，函数名 = embark_ui_open_<App 名>；
 *     生成代码吐一句裸调用，落到 app/common/aidogui_ui_hooks.h 声明的 C 入口，再转发到
 *     aidogui_ui_nav_request_app()。导航意图画在设计器里，实现只有这一处。
 *   - 点击绑定（aidogui_ui_nav_bind_click，备用）：不想在设计器里挂事件时，在 App 的
 *     onCreate 里声明"点屏 s 的控件 w → App a"；控件随屏销毁消失，回调装载后由本模块重挂。
 *   - 值重推（aidogui_ui_nav_bind_repaint，ADR 0015）：App 声明"屏 s 装载好后调我的 repaint"。
 *     与点击绑定对称、挂在同一个观察者上：屏是新的，所以**回调要重挂、控件值也要重推**；
 * 注意：生成代码里的 `_ui_screen_change()`（设计器里挂 CHANGE_SCREEN 动作）**不**经过本模块
 * —— 用它会绕过桥的"按需 init/destroy"纪律（ADR 0011 决策②⑦），所以导航别用那个动作。
 *
 * 什么时候调 attach：谁来当 UI 的宿主谁调（主屏 App 的 onCreate 最合适）。
 * attach 内部会 init（幂等），重复 attach 同一个框架是 no-op。
 */
#ifndef EMBARK_APP_AIDOGUI_UI_NAV_H
#define EMBARK_APP_AIDOGUI_UI_NAV_H

#include <embark/framework.h>

namespace embark::demo {

/// 把 AiDoGUI UI 接进框架：确保桥已 init（幂等），并装屏切换观察者。
void aidogui_ui_nav_attach(Framework& framework);

/// 卸掉观察者（测试/退出用；之后 AiDoGUI 里切屏不再影响框架）。
void aidogui_ui_nav_detach();

// --- 观测（验收用）---------------------------------------------------------
/// 累计"AiDoGUI 切屏 → 有同名 App → 登记了切换请求"的次数。
int aidogui_ui_nav_switch_requests();
/// 最近一次被观察到的屏名（没发生过返回 nullptr；指针指向屏表里的静态字符串）。
const char* aidogui_ui_nav_last_screen();

// --- 导航请求（设计器事件与点击绑定的公共入口）------------------------------
/// 请求切到 App <app_name>。返回 false = 名字为空或没有同名 App（只记日志）；
/// 目标已在前台时是 no-op（返回 true），不会重复 onEnter。设计器里的「调用函数」事件
/// （embark_ui_open_<App 名>，见 aidogui_ui_hooks.h）就是转到这儿的。
bool aidogui_ui_nav_request_app(const char* app_name);

// --- 点击绑定（屏上的控件 → App / 逻辑动作；设计器事件的备用路径）------------
/// 声明一条点击导航：点 AiDoGUI 屏 <screen_name> 里的控件 <widget_name> → 切到 App
/// <app_name>。App 在 onCreate 里声明一次就够。**不要**自己给控件加事件回调：控件每次
/// 装载都是新对象，回调由本模块在装载后统一重挂（自己再挂一次会点一下就切两次）。
/// 返回 false = 参数为空或表已满（上限 8 条）；控件名写错不算失败（装载时才发现，只记日志）。
bool aidogui_ui_nav_bind_click(const char* screen_name, const char* widget_name,
                               const char* app_name);

/// 声明一条**逻辑**点击：点控件 <widget_name> → 就地调 <action>（不切 App）。用于屏上
/// "有个按钮要干点事"的场合（如设置的「亮度 +1」）；重挂时机与 bind_click 相同。
/// 同一个控件先后声明两种绑定时，后写的生效（共用一张表）。返回 false = 参数为空或表已满。
bool aidogui_ui_nav_bind_action(const char* screen_name, const char* widget_name, void (*action)());

// --- 控件值重推（屏装载后 → App 的 repaint 钩子；与点击绑定对称）--------------
/// 声明"屏 <screen_name> 装载好后 → 调 <repaint>"，让 App 把自己负责的**控件值**重推一遍。
/// App 在 onCreate 里声明一次就够。
///
/// 为什么要有这条：桥的纪律是"切走即 destroy / 回来即 init"（ADR 0011 决策②），屏每次回来
/// 都是**新对象**、值回到设计稿写死的那些；所以**控件值的缓存必须跟着屏一起失效**。回调由
/// 本模块在装载后自动重挂，控件值重推同样由本模块在装载后自动调用 —— 漏掉一次就是"回主屏
/// 时间停在设计稿 11:31"那类 bug（ADR 0015）。**不要**自己在 onEnter 里再推一遍：装载屏
/// 发生在 onEnter/onResume 里，推早了推给的是上一块屏。
/// 调用时机与点击绑定相同：桥装载完新屏、销毁完旧屏之后，<repaint> 里可以直接写控件。
/// 返回 false = 参数为空或表已满（上限 8 条）；同一屏重复声明时后写的生效。
bool aidogui_ui_nav_bind_repaint(const char* screen_name, void (*repaint)());

/// 已登记的值重推条数（验收用）。
int aidogui_ui_nav_repaint_bindings();
/// 累计"屏装载后重推了控件值"的次数（验收用）。
int aidogui_ui_nav_repaint_calls();

/// 已登记的点击绑定条数（验收用）。
int aidogui_ui_nav_click_bindings();
/// 累计"经本模块登记了切换请求"的次数（设计器事件 + 点击绑定都算；验收用）。
int aidogui_ui_nav_click_hits();

}  // namespace embark::demo

#endif /* EMBARK_APP_AIDOGUI_UI_NAV_H */
