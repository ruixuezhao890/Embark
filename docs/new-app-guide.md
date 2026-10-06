# 新手指南：30 分钟加一个带界面的 App

> 目标读者：第一次在 Embark 上加 App 的开发者（人，或 agent）。
> 读完并走完本指南，你会掌握 Embark 的完整 App 工作流——**C++ 侧建壳 → EEZ Studio 画同名屏 →
> 变量桥接线 → 构建验收**。之后加任何 App 都是同一个套路。
> 前置条件：仓库能构建（见根 README「宿主构建」）；EEZ Studio 已安装（[eez-studio-guide.md](eez-studio-guide.md) 第 1 步）。
> 接口速查与完整 API 见 [eez-ui-manual.md](eez-ui-manual.md)（本指南是流程，那是词典）。

## 0. 三个角色，一张分工表

| 谁 | 管什么 | 在哪做 |
| --- | --- | --- |
| **EEZ Studio** | 画页面、排控件、配交互、声明显示变量 | 桌面工程（`.eez-project`，**仓库外**维护） |
| **Embark App（C++）** | 业务逻辑：状态机、定时任务、消息、数据 | `app/<名字>/` 下，6 个钩子 |
| **薄桥 `app/common/eez_ui_bridge.h`** | 生成代码 ↔ App 契约的唯一适配点 | App 只调它，不 include 生成代码头 |

铁的约定只有两条（手册 §2）：**EEZ 屏名 == App 名**；**Flow 全局变量名 = `<app名>_<字段>`**。
其余都是照葫芦画瓢。

## 1. 建 App 壳（C++ 侧，10 分钟）

仓库里已有三个薄壳样例：`app/launcher/`（主屏 + 导航接线）、`app/clock/`（tick + 状态机 + 收 `BrightnessMessage`）、`app/settings/`（suspend + 逻辑入口），照 `app/clock/` 复制改名即可。


1. **复制并改名**：`app/clock/clock_app.{h,cpp}` → `app/my_app/my_app.{h,cpp}`，类名 `HelloApp` → `MyApp`，
   `name()` 返回 `"my"`（唯一、小写），`title()` 返回中文标题（如 `"我的"`，元数据用）。

2. **`app/CMakeLists.txt`**：`embark_demo_apps` 源列表加 `my_app/my_app.cpp`。

3. **`platform/host/ui_demo.cpp`**：`EMBARK_APP_TABLE(...)` 里加 `embark::demo::MyApp`
   （放在 `LauncherApp` 之后，首位必须是启动器）。

4. **构建验证**（此时 Studio 还没画屏，能过吗？能——薄壳是"缺屏安全"的）：

```sh
cmake --build build
./build/platform/host/embark_host_ui --frames 60   # Windows: .\\build\\...\\embark_host_ui.exe
```

会看到 launch 日志里 App `my` 进入前台时打印一条告警（`enter_app_screen` 找不到同名屏 →
保持当前屏 + 告警，见手册 §3），界面照常，不会黑屏。

## 2. 画同名屏（EEZ Studio 侧，15 分钟）

### 2.1 工程从哪来（先弄清这件事，别卡在这）

仓库 **只存导出产物**（`app/eez_ui/src/ui/`，生成代码，手改会被下次导出覆盖）；
源工程 `.eez-project` 是 Studio 的二进制文件，**不在仓库**（二进制易冲突、与 Studio 版本绑定）。
所以：

- **已有 Embark 的源工程**（向维护者要 `.eez-project` 打包，或随发布包附带）→ 直接打开，跳到 2.2。
- **没有源工程** → 不要自己从零建全套 UI：成本高且容易和既有 launcher/clock 屏脱节。
  走第 1 节的"缺屏安全"模式先跑通 C++ 侧，拿到源工程后再回来画。

### 2.2 新建页面，屏名必须等于 App 名

Structures 面板 → 加页面，命名 **`my`**（严格等于 `name()`，子页用 `<app名>_<编号>_sub`）。
画布 240×320（宿主竖屏）。

### 2.3 放控件

拖文本/值显示/按钮等控件，样式在 Studio 里排（深色科技风参考 launcher 屏的配色）。
**文案用 ASCII**（EEZ 屏当前用 LVGL 默认字体 Montserrat，不含中文；中文需要字库方案，见 §5 踩坑）。

### 2.4 声明 Flow 全局变量

Flow 面板 → Variables → 新建 `my_visit_count`（int 类型，初值 0）。
命名约定 `<app名>_<字段>`，这样 App 侧的变量桥按名对得上。

### 2.5 把控件绑到变量

值显示控件的属性面板 → 文本/值属性 → 选「绑定变量」→ 选 `my_visit_count`。
生成代码每帧 `tick_screen_my()` 会把变量值刷进控件（由 App 侧 `eez_ui_bridge_tick()` 驱动）。

### 2.6 按钮跳转：SetPage，别用「回上一屏」

放一个按钮「回主屏」→ 交互设为 **SetPage → launcher**。
⚠ 不要用「回上一屏（changeToPreviousScreen）」：EEZ 屏栈恒空，pop 是 no-op，点了没反应
（历史上 clock 屏按钮配成过它，对应坑详细见手册 §6）。

### 2.7 导出 + 重配构建

导出代码到 `app/eez_ui/src/ui/`（覆盖现有生成文件），然后：

```sh
cmake -S . -B build        # 必须重配：配置日志打印「EEZ 屏表：N 个」「EEZ 变量：1 个」
cmake --build build
```

变量表是**构建期解析**的（`cmake/embark_eez_vars.cmake` → `build/include/embark_eez_vars.h`），
Studio 里加/删变量后只要重配即可，不用改 C++、不用动 CMakeLists。

## 3. 接线（C++，5 分钟）

在 `MyApp` 的四个钩子里调桥接口。完整接线版（照抄，改名字即可）：

```cpp
// my_app/my_app.h —— 薄壳：不需要任何 LVGL 成员，只有钩子声明
#include <embark/app.h>
#include <embark/framework.h>

class MyApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "my"; }
  [[nodiscard]] const char* title() const override { return "我的"; }
  void onCreate(embark::Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

 private:
  std::uint32_t visits_ = 0;   // 业务数据
};
```

```cpp
// my_app.cpp
#include <embark/log.h>
#include "my_app/my_app.h"
#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#endif

void MyApp::onCreate(embark::Framework& fw) {
  (void)fw;
  ELOG_INFO("App {} onCreate", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_ensure_init();          // 幂等：谁先来谁启动生成代码
#endif
}

void MyApp::onEnter() {
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());  // 挂同名屏；缺屏 → 保持当前屏 + 告警
#endif
}
void MyApp::onResume() {
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());  // 回前台时再挂一次自己的屏
#endif
}

void MyApp::onForegroundTick(std::uint32_t) {
#if defined(EMBARK_EEZ_UI_BRIDGE)
  ++visits_;
  eez_ui_bridge_set_var_int("my_visit_count", int(visits_));  // 推数据给 UI
  eez_ui_bridge_tick();                                       // 推进 Flow（变量刷进控件）
#endif
}
```

想再勤快点：`onPause/onExit` 只打日志（薄壳惯例）。

**UI 事件反过来进 C++ 的两条路**：
- 切屏（按钮 SetPage）→ 框架自动：`eez_ui_nav` 观察者把切屏翻成 `request_switch(同名 App)`，
  主屏 App 的 `onCreate` 里 `eez_ui_nav_attach(fw)` 一次即可（`app/launcher/launcher_app.cpp` 就是样板）。
- 业务动作（发消息、改状态）→ User Action：Studio 里定义 action，C++ 实现对应函数
  （ADR 0008 决策 4：User Action 在生成代码的 Flow action 里实现，由 Studio 生成的 dispatch 表进入 C++，位置见 `app/eez_ui/src/ui/eez-flow.cpp`；也可以改成自己 App 目录里的实现文件，只要 CMake 源列表里登记）。

## 4. 构建与验收（5 分钟）

```sh
cmake -S . -B build && cmake --build build
./build/platform/host/embark_host_ui --frames 60      # 看着你的屏出来、变量数字在跳
./build/platform/host/embark_host_tour                # 系统用例：不回归已有 App
ctest --test-dir build --output-on-failure            # 单测：屏表/变量表自检在里面
```

缺屏/缺变量时的安全行为（都是"照常运行 + 日志"，不崩）：

| 情况 | 行为 |
| --- | --- |
| Studio 没画同名屏 | `enter_app_screen` 保持当前屏 + ELOG_WARN 告警（不黑屏） |
| `set_var` 的变量名 Studio 没声明 | 返回 false，无副作用；构建期变量表里也没有该名字 |
| 屏名拼错（≠ App 名） | 同上，挂不上屏 |
| Studio 加了变量忘了重配 | 变量表没刷新，桥查不到 → set/get false；重配即可 |

## 5. 踩坑清单（新用户第一周必看）

1. **屏名 ≠ App 名** → 挂不上屏（症状：一直在上一屏 + 告警日志）。
2. **按钮用了「回上一屏」** → 点了没反应（屏栈恒空）。一律 SetPage。
3. **改了变量/屏没重新 cmake 配置** → 桥查不到、`set_var` 一直 false。
4. **手改生成文件**（`app/eez_ui/src/ui/`）→ 下次导出被覆盖；改页面去 Studio。
5. **EEZ 屏中文文案** → 默认字体 Montserrat 无中文；当前仓库的字库方案是
   `tools/font/gen_font.mjs` 生成的静态子集（`assets/fonts/embark_zh_14.c`，导航壳退役后保留备用，
   需 `LV_USE_FONT_COMPRESSED=1`，见 `config/lv_conf.h:100-107`）。新 App 的中文界面要复用这套
   （在 Studio 里引用该字体并把文案纳入缺字审计，issue 17）。
6. **app 名字冲突** → 注册表里必须唯一（`EMBARK_APP_TABLE` 展开时同名 App 编译期报错）。

## 6. 从这里往哪走

- 桥的完整 API、命名约定细节：**[eez-ui-manual.md](eez-ui-manual.md)**（词典）
- Studio 安装/导出/注意事项：**[eez-studio-guide.md](eez-studio-guide.md)**
- 后台策略 / 消息总线 / own_task 生命周期：**[messages-and-background.md](messages-and-background.md)**
- 适配层设计（切屏、User Action、变量表）：**[adr/0008-eez-studio-adapter.md](adr/0008-eez-studio-adapter.md)**

> 想要脚本化起步（自动生成薄壳 + 注册清单 + 变量骨架）？`python tools/scaffold_app.py --help`
> （生成后可用 `--vars-check build/include/embark_eez_vars.h` 核对变量表）。
