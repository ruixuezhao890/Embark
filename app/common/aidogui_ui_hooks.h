/**
 * AiDoGUI 生成代码 → 桥的 C 入口（ADR 0011 决策⑦）。
 *
 * 设计器里给控件挂「调用函数」动作时，生成代码原样吐一句裸调用
 * （`embark_ui_open_clock();`）：它不 include 我们的头，也不认识命名
 * 空间。所以本头由 app/aidogui_ui/CMakeLists.txt 用编译器的强制注入
 * （GCC/Clang 的 -include）塞进每个生成 C 文件，生成代码保持只读。
 *
 * 命名约定：函数名 = embark_ui_open_<App 名>，一个 App 一行（见下表）。
 * 加 App 要三处同步：这里的表、设计器事件里的「函数名」、App 的同名屏。
 *
 * 实现在 app/common/aidogui_ui_nav.cpp：全部转发给
 * aidogui_ui_nav_request_app()，于是「设计器里点的动作」与「框架切前台
 * App」是同一条路 —— 不会绕过「切走即 destroy / 回来即 init」的纪律。
 */
#ifndef EMBARK_APP_AIDOGUI_UI_HOOKS_H
#define EMBARK_APP_AIDOGUI_UI_HOOKS_H

/* 能被界面动作打开的 App（设计器「函数名」→ embark_ui_open_<名字>）。 */
#define EMBARK_AIDOGUI_HOOK_APPS(X) \
  X(launcher)                       \
  X(clock)                          \
  X(settings)                        \
  X(weather)

#ifdef __cplusplus
extern "C" {
#endif

#define EMBARK_AIDOGUI_HOOK_DECLARE(app) void embark_ui_open_##app(void);
EMBARK_AIDOGUI_HOOK_APPS(EMBARK_AIDOGUI_HOOK_DECLARE)
#undef EMBARK_AIDOGUI_HOOK_DECLARE

#ifdef __cplusplus
}
#endif

#endif /* EMBARK_APP_AIDOGUI_UI_HOOKS_H */
