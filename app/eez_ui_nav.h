/**
 * EEZ 屏 → App 的导航桥（issue 19 / ADR 0008）：把"EEZ 里切屏"变成"框架切前台 App"。
 *
 * 分工：
 *   - app/eez_ui_bridge.h  只管 UI 生成代码（启动/tick/屏表/切屏/切屏观察者）；
 *   - app/eez_ui_screen_names.h 管屏名→App 名的纯解析（可单测）；
 *   - 本文件把两边接起来：装观察者 → 屏名解析成 App 名 → framework.request_switch。
 *
 * 于是 EEZ Studio 里画一个按钮、让它 SetPage 到某屏，就等于"启动那个 App"：
 * 屏名 == App 名（App 的子页 <app名>_<编号>_sub 归到它的 App）。没有同名 App 的
 * 屏（还没实现/纯展示页）只记一条日志，不切前台 —— 不会因为 UI 里多画一屏就崩。
 *
 * 什么时候调 attach：谁来当 EEZ UI 的宿主谁调（主屏 App 的 onCreate 最合适：框架
 * 先把所有 App 的 onCreate 跑完再进事件循环，所以观察者从第一帧起就是热的）。
 * attach 内部会 ensure_init（幂等），重复 attach 同一个框架是 no-op。
 */
#ifndef EMBARK_APP_EEZ_UI_NAV_H
#define EMBARK_APP_EEZ_UI_NAV_H

#include <embark/framework.h>

namespace embark::demo {

/// 把 EEZ UI 接进框架：确保生成代码已启动（幂等），并装屏切换观察者。
void eez_ui_nav_attach(Framework& framework);

/// 卸掉观察者（测试/退出用；之后 EEZ 里切屏不再影响框架）。
void eez_ui_nav_detach();

// --- 观测（验收用）---------------------------------------------------------
/// 累计"EEZ 切屏 → 有同名 App → 登记了切换请求"的次数。
int eez_ui_nav_switch_requests();
/// 最近一次被观察到的屏名（没发生过返回 nullptr；指针指向屏表里的静态字符串）。
const char* eez_ui_nav_last_screen();

}  // namespace embark::demo

#endif /* EMBARK_APP_EEZ_UI_NAV_H */
