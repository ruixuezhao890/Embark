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

仓库里已有三个薄壳样例：`app/launcher/`（主屏 + 导航接线）、`app/clock/`（tick + 状态机 + 收 `BrightnessMessage`）、`app/settings/`（suspend + 逻辑入口）。

### 1.1 一键生成（推荐，30 秒）

不用手工复制改名，跑脚本即可：

```sh
python tools/scaffold_app.py                       # 交互菜单：问答式填 App 名 / 标题 / 后台策略 / 变量，先预览再确认
```

或直接给参数（默认只预览，加 `--apply` 才落地）：

```sh
python tools/scaffold_app.py my --title "我的" --var visit_count:int --apply
```

脚本一次做完四件事：生成 `app/my/my_app.{h,cpp}` 薄壳、把源文件追加进 `app/CMakeLists.txt`
的锚点行、把 `embark::demo::MyApp` 追加进 `platform/host/ui_demo.cpp` 的 `EMBARK_APP_TABLE`、
并写出 `app/my/eez_vars.txt` —— **Studio 侧变量声明的粘贴清单**（见 §2.4）。

> **生成之后还要加变量？** 不用重新生成，也不会覆盖你写的业务代码：
> - 菜单：`python tools/scaffold_app.py` → 选 `2`（给已有 App 追加变量）
> - 命令行：`python tools/scaffold_app.py my --add-var count:int --add-var active:bool --apply`
>
> 新变量插到已有变量之后、`eez_ui_bridge_tick()` 之前；已在的变量自动跳过（幂等），
> `eez_vars.txt` 同步追加。单次向导最多填 12 个，**追加次数不限**。

### 1.2 手工方式（照做一遍更懂结构）

照 `app/clock/` 复制改名即可：

1. **复制并改名**：`app/clock/clock_app.{h,cpp}` → `app/my_app/my_app.{h,cpp}`，类名 `ClockApp` → `MyApp`，
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

仓库同时维护**导出产物**（`app/eez_ui/src/ui/`，生成代码，手改会被下次导出覆盖）与
**源工程**（`app/eez_ui/embark.eez-project`，Studio 二进制、约 23 KB，随仓库入库，ADR 0008）。

直接打开仓库里的 `app/eez_ui/embark.eez-project` → 跳到 2.2。
- **没有源工程** → 不要自己从零建全套 UI：成本高且容易和既有 launcher/clock 屏脱节。
  走第 1 节的"缺屏安全"模式先跑通 C++ 侧，拿到源工程后再回来画。

### 2.2 新建页面，屏名必须等于 App 名

Structures 面板 → 加页面，命名 **`my`**（严格等于 `name()`，子页用 `<app名>_<编号>_sub`）。
画布 240×320（宿主竖屏）。

> **屏的生命周期（内存）**：工程已勾 Settings→Build「Screens lifetime support」。你的**新屏不是
> 启动屏**：页面 General 里把 **createAtStart 关掉**（默认 true，别用默认）、**Delete on unload 打开**
> （离开即回收、回来重建）；launcher 保持常驻。详见 [eez-studio-guide.md](eez-studio-guide.md)
> 「屏生命周期」与 [eez-ui-manual.md](eez-ui-manual.md) §7。

### 2.3 放控件

拖文本/值显示/按钮等控件，样式在 Studio 里排（深色科技风参考 launcher 屏的配色）。
**文案用 ASCII**（EEZ 屏当前用 LVGL 默认字体 Montserrat，不含中文；中文需要字库方案，见 §6 踩坑）。

### 2.4 声明 Flow 全局变量

Flow 面板 → Variables → 新建 `my_visit_count`（int 类型，初值 0）。
命名约定 `<app名>_<字段>`，这样 App 侧的变量桥按名对得上。

`app/my/eez_vars.txt` 就是脚本给你的**粘贴清单**：每行写明变量全名与类型。
它是 C++ 侧 `eez_ui_bridge_set_var_int("my_visit_count", n)` 推的那个名字。

> **EEZ 侧的变量声明无法脚本化**：`.eez-project` 是 Studio 的二进制工程文件，只能在 Studio 里手工声明
> （或从维护者拿现成模板）。**两边名字必须对得上**（桥按名查找、比较时忽略大小写，但仍建议逐字一致）——对不上不会崩，
> `set_var_*` 找不到名字就返回 `false`（该次推送是 no-op），屏照常显示，只是那个控件不刷新。
> 构建后用 `python tools/scaffold_app.py --vars-check build/include/embark_eez_vars.h`
> 可以核对 C++ 侧变量表里有没有这个名字。

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

## 4. 钩子契约速查（8 个钩子：几次、哪个线程、能做什么）

> 一句话：**框架不认识「App 在干什么」**。没有 status / visible 字段可查，也不存在「先查询状态再决定调哪个钩子」——
> 全部是「事件驱动 + 定时器自发 + 每帧无条件」三类机制，外加一个「进过前台」的记账位。
> 所以写 App 真正要知道的不是框架内部判定，而是**分派契约**：下面这张表。

| 钩子 | 调用次数 | 线程 | 能做什么（纪律） |
| --- | --- | --- | --- |
| `onCreate(Framework&)` | 恰好 1 次（boot 装配期，按注册顺序） | UI | 存 `fw_ = &fw`、注入 HAL 能力（`fw.hal()`）、`eez_ui_bridge_ensure_init()`。**别 publish**：总线这时还没装配，发出去没人收（计 unknown + WARN）——要发消息放 `onEnter` 之后 |
| `onEnter()` | ≤ 1 次（第一次成为前台） | UI | 建屏 / 挂自己的 EEZ 屏、启动计时状态机；**同一帧框架武装这个 App 的后台策略**（issue 23 / ADR 0009）；声明 `ArmPolicy::at_boot` 的 App 在 boot 就已武装，这里不重复（issue 24 / ADR 0010） |
| `onPause()` | 每次离开前台 | UI | **只通知**：框架不碰你的 UI，保存状态请自己做 |
| `onResume()` | 每次回到前台 | UI | 恢复显示、**再挂一次屏**（你的屏可能已被别的 App 覆盖过） |
| `onForegroundTick(now_ms)` | 每帧一次（仅前台） | UI | 推数据给 UI：先 `set_var_*` 再 `eez_ui_bridge_tick()`。**必须轻**——它就在 UI 循环里 |
| `onBackgroundTick(now_ms)` | 武装之后：按 `settings().period_ms` 周期（默认没被打开过 = 一次都不跑；`ArmPolicy::at_boot` 的在 boot 第 9 步就武装） | **tick 策略 = UI 任务；own_task 策略 = 独立任务** | 轻活走 tick；阻塞/重计算走 own_task。own_task 里**不许碰 UI、不许 publish**，回 UI 只能 `post` 信封 |
| `onMessage(const Message&)` | 每条广播一次 | UI | 按 `msg.get_message_id()` 自分发。**v1 是广播：自己发的消息也会回到自己** |
| `onExit()` | 恰好 1 次 | UI | 收尾、打日志。**v1 只有关机路径会触发**，没有别的退役路径 |

最容易写错的三条（都有源码证据）：

1. **`onCreate` 里 publish 会丢**——总线在 `onCreate` 之后才装配（`src/embark/framework.cpp:44-54`），早于它的广播被计为 unknown 并 WARN（`src/embark/bus.cpp:66-71`）。装配消息请放 `onEnter` 之后。
2. **`onMessage` 会收到自己发的消息**——`publish` 遍历所有订阅者，**发送方的 adapter 也在订阅表里**（`src/embark/bus.cpp:55-75`）。要防自回环，就带个 `from` 字段自己过滤。
3. **同一个 `onBackgroundTick`，线程语义完全不同**——tick 策略在唯一 UI 任务里被调（`src/embark/framework.cpp:152` → `:196`），own_task 策略在自己的任务里被调（`:259` 一次性 / `:264` 常驻循环）。跨策略复制粘贴代码前先确认这条。

> 周期是**帧粒度**：`ceil(period_ms / ui_loop_period_ms)` 个帧（宿主 5 ms 一拍，`config/embark_limits.h:85`）。

**「要写的东西 → 放哪个钩子」对照**：

| 你要写的 | 放这里 | 理由 |
| --- | --- | --- |
| 建屏 / 手绘 LVGL 对象 | `onCreate` | 一次性，装配期做完 |
| 挂自己的 EEZ 屏 | `onEnter` + `onResume` | 首次进入 + 每次回来（屏会被覆盖） |
| 推数据给 EEZ 控件 | `onForegroundTick`：先 `set_var_*` 再 `tick` | 只有前台需要刷 |
| 读传感器 / 硬件数据 | 驱动写在 App 或平台侧，用 `fw.hal().bus` 取总线 | 数据怎么上屏见 [eez-ui-manual.md](eez-ui-manual.md) 第 5 节 |
| 耗时计算 / 网络 / 长阻塞 | `own_task` 策略 + `onBackgroundTick` | 不能阻塞 UI 任务 |
| own_task 里回 UI | `fw.post(CrossTaskMessage(...))`，回 UI 再进总线 | 跨任务只能 post 信封（[messages-and-background.md](messages-and-background.md)） |

> 框架**不会替你保存任何状态**：`entered_` 只是「进过前台」的记账位（`include/embark/framework.h:299`），
> 不是状态查询接口。要「记住」什么，自己存成员变量。
>
> 另外：声明了后台策略**默认也不会**一上电就跑 —— 第一次进过前台（`onEnter` 同一帧）才武装，
> 之后才可能出现 `onBackgroundTick`（查询用 `fw.background_armed(id)`）。要「一上电就跑」
> （闹钟这类由持久化状态驱动的 App）就显式声明 `ArmPolicy::at_boot`（issue 24 / ADR 0010）。详见
> [messages-and-background.md](messages-and-background.md) 的「武装时机」。

## 5. 构建与验收（5 分钟）

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

## 6. 踩坑清单（新用户第一周必看）

1. **屏名 ≠ App 名** → 挂不上屏（症状：一直在上一屏 + 告警日志）。
2. **按钮用了「回上一屏」** → 点了没反应（屏栈恒空）。一律 SetPage。
3. **改了变量/屏没重新 cmake 配置** → 桥查不到、`set_var` 一直 false。
4. **手改生成文件**（`app/eez_ui/src/ui/`）→ 下次导出被覆盖；改页面去 Studio。
5. **EEZ 屏中文文案** → 默认字体 Montserrat 无中文；当前仓库的字库方案是
   `tools/font/gen_font.mjs` 生成的静态子集（`assets/fonts/embark_zh_14.c`，导航壳退役后保留备用，
   需 `LV_USE_FONT_COMPRESSED=1`，见 `config/lv_conf.h:100-107`）。新 App 的中文界面要复用这套
   （在 Studio 里引用该字体并把文案纳入缺字审计，issue 17）。
6. **app 名字冲突** → 注册表里必须唯一（`EMBARK_APP_TABLE` 展开时同名 App 编译期报错）。

## 7. 从这里往哪走

- 桥的完整 API、命名约定细节：**[eez-ui-manual.md](eez-ui-manual.md)**（词典）
- Studio 安装/导出/注意事项：**[eez-studio-guide.md](eez-studio-guide.md)**
- 后台策略 / 消息总线 / own_task 生命周期：**[messages-and-background.md](messages-and-background.md)**
- 适配层设计（切屏、User Action、变量表）：**[adr/0008-eez-studio-adapter.md](adr/0008-eez-studio-adapter.md)**

> 脚本化起步：`python tools/scaffold_app.py`（无参数 = 交互菜单；`--help` 看全部参数）——
> 一条命令生成薄壳 + 注册 + 变量骨架与 `eez_vars.txt` 粘贴清单；
> `--add-var 字段:类型` 给已有 App 增量加变量（默认预览，`--apply` 落地）；
> `--vars-check build/include/embark_eez_vars.h` 核对 C++ 侧变量表与 Studio 声明是否对齐。
