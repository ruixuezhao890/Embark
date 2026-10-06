# Embark 文档

新手指南与主题文档。框架是什么、当前进度、命令行细节见根目录
[README](../README.md)；术语表见 [GLOSSARY](../GLOSSARY.md)；规格书（v1 定义）
在 `.scratch/embark-v1/spec.md`；工单在 `.scratch/embark-v1/issues/`。

## 最短路径

四步：**跑起来**（构建 + 运行 demo）、**写你自己的 main**（不退出、模拟单片机）、
**敲起来**（用命令行开关做自动验收）、**改起来**（照模板加一个 App）。

### 1. 跑起来（从零开始，约 15 分钟）

按下面顺序走，每一步都是在"新装的机器"上能直接执行的。

**① 装工具**（装好 `git --version`、`cmake --version`、`ninja --version` 能出号即可）：

- **Windows**：
  - Git：官方安装包（默认选项）；
  - CMake ≥ 3.24：官方安装包（装完把 `cmake` 加进 PATH）；
  - Ninja：独立发行版放进 PATH（或随 CMake 自带）；
  - MinGW-w64（C++17 编译器）：推荐 [winlibs.com](https://winlibs.com/) 的 UCRT 发行版（本仓库用 g++ 15 验证；宿主 FreeRTOS 端口是 MSVC-MingW，普通发行版即可）；
  - SDL2（宿主 UI 必需）：GitHub Releases 的 **SDL2-devel-*-mingw** 包，解压后 configure 时传
    `-DCMAKE_PREFIX_PATH=<解压目录>/x86_64-w64-mingw32`（CMake 会在那里按 CONFIG 模式找到
    SDL2Config.cmake；`SDL2.dll` 会被自动复制到 exe 旁边）。**没装 SDL2 时 UI 目标会被跳过**——
    build 成功但没有 `embark_host_ui.exe`；跑 UI 之前务必先装。
- **Linux（Debian/Ubuntu）**：

  ```sh
  sudo apt install git cmake ninja-build g++ libsdl2-dev
  ```

**② 拉取仓库（含依赖）**：5 个依赖（ETL / efmt-elog / LVGL / FreeRTOS / doctest）以 git submodule
引入，克隆时一次带齐：

```sh
git clone --recursive https://github.com/ruixuezhao890/Embark.git
cd Embark
```

已经 clone 过的仓库补拉依赖：`git submodule update --init --recursive`。

**③ 配置，④ 构建，⑤ 测试，⑥ 运行**：

```sh
cmake -G Ninja -B build                   # 配置（宿主 + 测试；SDL2 找不到只跳过 UI demo）
cmake --build build                       # 构建（零警告要求）
ctest --test-dir build --output-on-failure  # 单元测试（95 用例 / 835 断言）
./build/platform/host/embark_host_ui      # 宿主 UI demo（Windows: .\build\platform\host\embark_host_ui.exe）
./build/platform/host/embark_host_tour    # 系统用例：从启动看到任务切换（同上 Windows 加 .exe）
```

看到 240×320 的 LVGL 窗口（竖屏，标题 "Embark demo"，默认前台是 launcher App —— EEZ launcher 屏）就跑起来了。
不带参数 = **一直跑到关窗**（模拟单片机上电后常驻）。想自己写程序（不退出）看下一节 §2；
想"跑够帧数自己退"或自动验收，看第 3 节。

`embark_host_tour` 是"一条用例看完整系统"：不加参数就会自己走完
**启动 → 后台节拍 → 合成点击切前台 → App 间消息 → 再切回来 → 一次性任务跑完并回收 → 关窗收尾**，
边跑边在控制台用中文解说每一步（前台是谁、谁收到了消息、钩子跑了几次、任务池还剩几个槽），
最后打一张 15 项自检清单；全部通过退出码 0，任一项不满足退出码 2。
同一条流程的**无窗口版本**是 doctest 用例 `系统用例：从启动到任务切换走一遍`
（`tests/kernel/test_system_tour.cpp`），在 CLion 里单跑那一条即可从上往下读日志；
任务生命周期那一步的用例是 `系统用例：own task 创建 → 跑完 → 回收 → 再创建`
（`tests/kernel/test_own_task_lifecycle.cpp`）。

日志是整对象风格（issue 13）：类型自己在声明处登记怎么打，调用点直接填空，
比如启动时的 `HAL 就绪：显示 embark::hal::DisplayInfo { width = 240, height = 320, ... }`、
每个 App 的 `后台配置 embark::AppSettings { background = ..., period_ms = 100, ... }`。
加字段不用改日志行；新类型怎么登记见 [common-pitfalls.md](common-pitfalls.md) 的
"派生打印"一节（含 384 字节单行上限）。

### 2. 写你自己的 main：像真机一样跑（不退出）

上面 §1 的 `embark_host_ui` 是"演示 + 自动验收"二合一，开关都在它身上；你自己的程序
**不要从它改**。仓库另有一份干净的示例入口：`platform/host/user_main.cpp`
（构建目标 `embark_host_user`，`cmake --build build` 会一起编出
`build/platform/host/embark_host_user.exe`，不用改 CMake）。

它模拟"单片机烧录后上电"的常态：`main` 只做两件事——起唯一 UI 任务 + 进调度器
（`start_scheduler()` 永不返回 = "上电了"）；UI 任务装配好 HAL 与框架后进入 `for(;;)`
主循环（一拍 = 输入泵 → 前台切换 → 消息/后台节拍 → `lv_timer_handler()`），
**默认一直跑到关窗**：

```sh
./build/platform/host/embark_host_user     # Windows 加 .exe；像真机一样跑，直到你关掉窗口
```

骨架（完整可照抄文件就是 user_main.cpp，注释齐全；改成自己的程序只动三处）：

```cpp
int main(int argc, char** argv) {
  Options options = parse_options(argc, argv);   // 可省；--frames N 给脚本/CI 收尾用
  const Error e = hp::start_ui_task(&ui_main, &options);  // 起唯一 UI 任务
  if (e != Error::none) return 1;
  hp::start_scheduler();          // 永不返回 = "上电"，之后 UI 任务在跑
}

void ui_main(void* argument) noexcept {
  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(1, "我的程序");
  hp::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);
  if (hal.init() != Error::none) hp::exit_process(1);

  embark::platform::LvglUiPort ui_port(hal.context(), &host_exit_query, &input);
  static hp::HostTaskSpawner spawner;             // 静态：own_task 后台任务的槽位池
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (framework.boot() != Error::none) hp::exit_process(1);

  for (;;) {                                      // 一路跑到关窗
    framework.step();                             // 一拍：输入 → 前台切换 → 消息/节拍 → lv_timer_handler
    /* 你的每拍业务：推 UI 变量、读传感器、发消息…… */ ;
    hp::ui_loop_delay();                          // 让出 5 ms，绝不忙等
  }
}
```

改成你自己的程序，只动三处（文件里都标了）：**① `EMBARK_APP_TABLE(...)`** 换成你的
App 类（第一位 = 上电默认前台）；**②** `HostDisplay` 的窗口标题（可选）；**③** 主循环里
`framework.step()` 前后想做的每拍业务。

**退出语义**：默认关窗 = 停止模拟（`SDL_QUIT` / 点 X / Alt+F4，像按了电源）；想"永不退出"
（真机 v1 的样子）就把 `LvglUiPort` 的退出查询参数传 `nullptr`。`--frames N` 只是
脚本/CI 用的收尾便利（`--help` 有清单），平时不用。

`embark_host_ui` 上的 `--click / --launch / --eez / --drag` 都是**自动验收开关**，不是
常态运行方式；想看"系统整条生命周期 + 自检清单"用 `embark_host_tour`（见第 3 节末尾）。

### 3. 敲起来

宿主 UI 可执行文件的开关都是为自动验收做的（`--help` 有完整清单）：

```sh
./build/platform/host/embark_host_ui --click                     # 最短验收：launcher ⇄ clock（点 EEZ 屏按钮往返），退出码 0
./build/platform/host/embark_host_ui --frames 200 --launch
# 合验：request_switch + EEZ 屏按钮往返切前台 4 次（含亮度消息 1 发 1 收），退出码 0
```

退出码约定：0 = 全部验收通过；2 = 某项断言没满足（日志里 `验收失败` 会说出
是哪个 App、期望什么）；1 = 启动失败。详细的开关表见根 README。

### 4. 改起来：加一个只显示一行字的 App

照着 demo 的模板走，总共五步：

1. **新建 `app/hello/hello_app.h`**（示例名 hello；每个 App 独立目录 `app/<名字>/`。薄壳：不 include LVGL、不持有屏对象，界面全部交给 EEZ）：

```cpp
#ifndef EMBARK_APP_HELLO_APP_H
#define EMBARK_APP_HELLO_APP_H

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class HelloApp final : public App {
 public:
  HelloApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "hello"; }
  [[nodiscard]] const char* title() const override { return "你好"; }  // 中文标题（元数据）

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;
};

}  // namespace embark::demo

#endif
```

2. **新建 `app/hello/hello_app.cpp`**（薄壳纪律：不建屏、不手绘；进入前台只按屏名挂自己的 EEZ 屏）：

```cpp
#include <embark/log.h>

#include "hello/hello_app.h"

#if defined(EMBARK_EEZ_UI_BRIDGE)
#include "eez_ui_bridge.h"
#endif

namespace embark::demo {

void HelloApp::onCreate(Framework& fw) {
  (void)fw;
  ELOG_INFO("App {} onCreate（薄壳：界面 = EEZ 屏 hello，Studio 画好即生效）", name());
}

void HelloApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());   // 屏名约定：screen 名 == App 名
#endif
}

void HelloApp::onPause() { ELOG_INFO("App {} 离开前台", name()); }

void HelloApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_enter_app_screen(name());
#endif
}

void HelloApp::onExit() { ELOG_INFO("App {} onExit", name()); }

void HelloApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_EEZ_UI_BRIDGE)
  eez_ui_bridge_tick();   // 前台每帧驱动 EEZ Flow
#endif
}

}  // namespace embark::demo
```

3. **`app/CMakeLists.txt`**：`embark_demo_apps` 的源列表加一条 `hello/hello_app.cpp`（现状就是目录化布局）：

```cmake
add_library(embark_demo_apps STATIC
        launcher/launcher_app.cpp
        clock/clock_app.cpp
        settings/settings_app.cpp
        common/eez_ui_bridge.cpp
        common/eez_ui_nav.cpp
        hello/hello_app.cpp)   # 你新增的 App（追加在真机清单锚点处）
```

4. **`platform/host/ui_demo.cpp`**：`EMBARK_APP_TABLE(...)` 里加进注册表
（`LauncherApp` 必须第一位：默认前台 + `request_home()` 目标 + 主屏）：

```cpp
EMBARK_APP_TABLE(embark::demo::LauncherApp,  // 首位必须是启动器
                 embark::demo::ClockApp,
                 embark::demo::SettingsApp,
                 embark::demo::HelloApp)     // 你的新 App
```

5. **在 EEZ Studio 画 `hello` 屏**（屏名必须等于 App 名）：不画也能编译运行——
`enter_app_screen` 找不到同名屏时保持当前屏 + 一条告警（缺屏策略 A），画好同名屏后自动生效。

6. **重新构建并跑**：

```sh
cmake --build build
./build/platform/host/embark_host_ui --frames 90 --click   # 最短验收：launcher ⇄ clock 往返
```

想让它被启动：在 EEZ Studio 的 launcher 屏加一个按钮，Flow SetPage 到 `hello` 屏——
屏观察者自动 `request_switch("hello")`。不需要改任何框架代码——App 只是注册表里多了一项。
想让它退后台后做点事（周期任务/自己的任务/发消息给别的 App），读 `messages-and-background.md`。

> 注意：界面文案全部在 EEZ Studio 里设置（字体在工程里配）；日志中文没问题。


### 用 EEZ Studio 画界面（手绘 UI 已退役，全程只调接口）

界面（外观、交互、切屏）全部由 EEZ Studio 负责，App 侧一行 LVGL 都不用写，
只需在 4 个钩子里调用薄桥接口（`app/common/eez_ui_bridge.h`，命名空间 `embark::demo`）：

1. `onCreate` → `eez_ui_bridge_ensure_init()`（幂等启动生成代码）。
2. `onEnter` / `onResume` → `eez_ui_bridge_enter_app_screen(name())`（按屏名约定挂自己的屏）。
3. `onForegroundTick` → 先 `set_var_*(...)` 推 UI 显示数据（Flow 全局变量），再 `eez_ui_bridge_tick()`。
4. 主屏（launcher）额外 `eez_ui_nav_attach(fw)`：EEZ 里 SetPage 切屏 = 框架切到同名 App。

**新手从零加 App 的端到端流程（建壳 → 画同名屏 → 绑变量 → 构建验收）见
[new-app-guide.md](new-app-guide.md)**；接口全集见 [eez-ui-manual.md](eez-ui-manual.md)（用户手册）；
Studio 安装/导出见 [eez-studio-guide.md](eez-studio-guide.md)。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [quickstart.md](quickstart.md) | **5 分钟快速开始**：跑起来 → 最短验收 → 加一个带界面的 App；常见问题速查 |
| [messages-and-background.md](messages-and-background.md) | 消息（总线 / 收件箱信封）与三种后台策略怎么用，含示例代码；`own_task` 的生命周期（入口返回 = 结束、每帧回收、运行期再创建）也在这里 |
| [hal-backend-guide.md](hal-backend-guide.md) | 怎么写一个 HAL 后端：宿主骨架（照 platform/host/）、共享层（platform/common/）与真机实现（platform/esp32/，含 IDF 坑清单） |
| [../platform/esp32/README.md](../platform/esp32/README.md) | ESP32-S3 真机端口：板级参数、构建/烧录命令、bring-up 清单、串口日志样例 |
| [new-app-guide.md](new-app-guide.md) | **新手指南**：30 分钟加一个带界面的 App（建壳 → 画同名屏 → 绑变量 → 构建验收，端到端） |
| [eez-ui-manual.md](eez-ui-manual.md) | EEZ UI 用户手册：界面交给 EEZ、App 只调 4 个接口（薄桥 API 全集 + 命名约定 + 模板） |
| [eez-studio-guide.md](eez-studio-guide.md) | EEZ Studio 一条龙：装 Studio、建工程、导出代码入库（源工程 .eez-project 随库入库） |
| [common-pitfalls.md](common-pitfalls.md) | 常见坑：ETL 定容行为、消息非聚合、保留 id、无异常/无堆、MinGW 对齐分配、日志 384 字节上限、派生打印（E_FMT_DERIVE）、宏前置条件…… |
| [adr/](adr/) | 架构决策记录：单一 UI 任务（0001）、HAL 能力粒度（0002）、零堆无异常（0003）、后台节拍与状态范式（0004）、静态槽位与任务池（0005） |
| [agents/](agents/) | 面向 agent 的仓库约定（领域模型、issue 追踪规则） |

交叉参考：内核契约测试在 `tests/kernel/`（发消息、切前台、节拍、own_task 生命周期与
任务池装配的"正确用法"都在测试里，比任何文档都新）；`.scratch/embark-v1/spec.md`
§5–§10 是接口设计与约束的来源。