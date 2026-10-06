# EEZ UI 用户手册：界面交给 EEZ，App 只调接口

> 适用对象：Embark App 的开发者。读完这份手册你就知道——**界面（外观、交互、切屏）全部
> 由 EEZ Studio 负责，App 侧一行 LVGL 都不用写**，只需要在 4 个钩子里调用薄桥的接口。
> 旧的手绘 UI 代码（lv_obj_create / lv_scr_load / 按钮回调）已全部退役并从仓库删除。

## 1. 分工一句话

| 谁 | 管什么 | 在哪做 |
| --- | --- | --- |
| **EEZ Studio** | 画页面、排控件、配交互（按钮跳转）、声明显示变量 | 桌面的 Studio 工程（.eez-project，导出到 app/eez_ui/src/ui/） |
| **Embark App（C++）** | 业务逻辑：状态机、定时任务、消息、数据 | app/ 下各 App 的 4 个钩子 |
| **薄桥（app/eez_ui_bridge.h）** | 生成代码 ↔ App 契约之间的唯一适配点 | App 只调它，不 include 生成代码头 |
| **导航接线（app/eez_ui_nav.h）** | EEZ 里切屏 → 框架切到同名 App | 主屏 App 的 onCreate 里 eez_ui_nav_attach(fw) 一次 |

## 2. 两个命名约定（务必遵守）

1. **屏名约定（critical）**：EEZ 里 screen 名 == App 名；子页 = <app名>_<编号>_sub。
   桥按这个约定给 App 找屏、把 EEZ 切屏翻成 request_switch。例如 App clock ↔ 屏 clock。
2. **变量命名约定**：Flow 全局变量名 = <app名>_<字段>，例如 launcher_tap_count、clock_hour。
   变量表在构建期从生成代码解析（cmake/embark_eez_vars.cmake → build/include/embark_eez_vars.h），
   Studio 里加/删变量后重新 cmake 配置即可，不用改 C++。

## 3. App 侧只需调 4 个接口

所有接口都在 embark::demo 命名空间（#include "eez_ui_bridge.h"），
生成代码的符号不会泄漏进你的编译面：

| 钩子 | 调什么 | 干什么 |
| --- | --- | --- |
| onCreate | eez_ui_bridge_ensure_init()（幂等，谁先来谁做） | 启动 EEZ 生成代码（Flow + 建屏 + 接管切屏事件） |
| onEnter / onResume | eez_ui_bridge_enter_app_screen(name()) | 按屏名约定加载自己的屏；还没画同名屏 → 保持当前屏 + 告警，不会黑屏 |
| onForegroundTick | eez_ui_bridge_tick()（每帧一次） | 推进 Flow 状态机（EEZ 的 tick_screen_* 靠它每帧把变量刷进控件） |
| 任意时刻 | eez_ui_bridge_set_var_*(name, v) / get_var_*(name, out) | 读写 Flow 全局变量（UI 显示数据的接口） |

变量名不存在、或 UI 还没 init，set/get 一律返回 false 且无副作用——所以 App 照常写，
不关心 Studio 是否已经声明了那个变量。

## 4. 最小模板（照抄即可）

```cpp
// my_app.cpp —— MyApp 的 .h 里声明薄壳成员即可，不需要任何 LVGL 成员
#include <embark/log.h>
#include "eez_ui_bridge.h"
#include "my_app.h"

namespace embark::demo {

void MyApp::onCreate(Framework&) {
  eez_ui_bridge_ensure_init();   // 幂等：主屏先启动，你再来也是 no-op
}

void MyApp::onEnter() {
  eez_ui_bridge_enter_app_screen(name());  // 加载同名 EEZ 屏
}

void MyApp::onResume() {
  eez_ui_bridge_enter_app_screen(name());  // 回前台时显示器可能还挂着别的屏，重新挂自己的
}

void MyApp::onForegroundTick(std::uint32_t) {
  eez_ui_bridge_set_var_int("clock_tick_count", ticks_);  // 例：把业务计数推给 UI
  eez_ui_bridge_tick();  // Flow 状态机每帧推进，变量的值经 tick_screen_clock() 刷进控件
}

}  // namespace embark::demo
```

真实样板：`app/launcher/launcher_app.cpp`（主屏 + 导航接线）、`app/clock/clock_app.cpp`（变量桥 + 收 `BrightnessMessage`）、
`app/settings/settings_app.cpp`（suspend + `bump_level` 逻辑入口）。

## 5. 数据怎么进 UI（Flow 全局变量）

1. 在 EEZ Studio 里声明 Flow 全局变量（名字按第 2 节的 <app名>_<字段> 约定）。
2. 在 Studio 里把控件绑定到该变量（属性面板里选变量）。
3. 重新导出代码到 app/eez_ui/src/ui/，重新 cmake 配置（cmake -S . -B build，日志会打印
   「EEZ 变量：N 个」）。
4. App 侧 onForegroundTick 里 set_var_*(...) 写值，生成的 tick_screen_*() 每帧把值刷进控件。

反向（UI → App / 按钮事件）同样在 Studio 里接：按钮的 Flow 动作里可以 SetPage 切屏
（见第 6 节），eez_ui_nav 的观察者会把这次切屏翻成 framework.request_switch(同名 App)。
需要把 UI 事件直接变成 C++ 业务动作（发消息、改状态）时，读 ADR 0008 的 User Action 方案
（app/eez_ui/actions.*，由 C++ 实现 action 函数）。

## 6. Studio 里按钮跳转：用 SetPage，别用「回上一屏」

导航模型是**纯替换**（非栈式）：EEZ 屏栈恒空，按钮做「回上一屏（changeToPreviousScreen）」
会 pop 空栈 → 什么都没发生（曾经把 clock 屏按钮配成它，点下去完全没反应）。
按钮要跳转，一律接 **SetPage → 目标屏**（与 launcher 屏按钮对称）。

## 7. 桥的完整接口（查缺补漏用）

```cpp
// 启动/运行：
eez_ui_bridge_init();                    // = ui_init()，onCreate 调一次
eez_ui_bridge_ensure_init();             // 幂等版 init
eez_ui_bridge_tick();                    // = ui_tick()，onForegroundTick 每帧调
eez_ui_bridge_load_current_screen();     // 把 Flow 当前页 lv_scr_load 上来
int eez_ui_bridge_current_screen();       // 当前页号（1 起；0 = 未初始化）

// 屏表（构建期从生成代码解析，加屏不用改 C++）：
int eez_ui_bridge_screen_count();
const char* eez_ui_bridge_screen_name(int i);          // 越界返回 nullptr
int eez_ui_bridge_screen_id(const char* name);         // 未命中 kEezScreenNone
bool eez_ui_bridge_load_screen(int id);
bool eez_ui_bridge_load_screen_by_name(const char* n);
bool eez_ui_bridge_load_screen_for_app(const char* app);
bool eez_ui_bridge_enter_app_screen(const char* app);  // onEnter/onResume 标准动作

// Flow 全局变量（UI 显示数据接口）：
int eez_ui_bridge_var_count();
const char* eez_ui_bridge_var_name(int i);             // 越界返回 nullptr
int eez_ui_bridge_var_index(const char* name);        // 未命中 kEezVarNone
bool eez_ui_bridge_set_var_int/float/bool/string(name, v);
bool eez_ui_bridge_get_var_int/float/bool(name, out);

// 切屏观察者（反向：EEZ 切屏 → App 听到）：
using EezScreenObserver = void (*)(const char* screen_name, void* user);
void eez_ui_bridge_set_screen_observer(EezScreenObserver, void* user);  // nullptr 卸掉
```

「屏名 → App」的完整反向接线在 app/eez_ui_nav.h：eez_ui_nav_attach(fw)（主屏 App 的
onCreate 调一次，幂等）之后，EEZ 里 SetPage 切到某屏 = 框架切到同名 App。
观测接口：eez_ui_nav_switch_requests()（累计切换次数）、eez_ui_nav_last_screen()（最近切到的屏名）。

## 8. 编译相关

- 生成代码在 app/eez_ui/src/ui/：这是 **Studio 导出产物**，改页面去 Studio 改再导出，
  不手改生成文件；app/eez_ui/CMakeLists.txt 用 file(GLOB ... CONFIGURE_DEPENDS)，
  导出新增/删减文件都不会破坏 CMake。
- 生成代码按第三方纪律编译（-w），CI 格式门禁已排除 app/eez_ui/。
- 宿主（PC 模拟器）定义 EMBARK_EEZ_UI_BRIDGE=1 让桥调用生效；esp32 真机不定义，
  桥调用编译成 no-op（App 逻辑照常，界面后续接真机工程时再开）。
- 变量名/屏名都不存在时桥接口安全返回（false / nullptr / kEez*None），不会崩。

## 9. 快速验证

```sh
cmake -S . -B build         # 配置：日志打印「EEZ 屏表：N 个」与「EEZ 变量：N 个」
cmake --build build
build/platform/host/embark_host_ui --eez   # 五档验收跑通（--launch/--click/--drag/--own-task/--eez）
build/platform/host/embark_host_tour       # 系统用例走查
ctest --test-dir build                    # 单元测试
```

## 10. 相关文档

- [eez-studio-guide.md](eez-studio-guide.md)：EEZ Studio 安装、建工程、导出代码的完整流程
- [adr/0008-eez-studio-adapter.md](adr/0008-eez-studio-adapter.md)：适配层架构决策
- [adr/0006-launcher-and-nav-shell.md](adr/0006-launcher-and-nav-shell.md)：导航壳与主屏
- [README.md](../README.md)：仓库总览
