# Embark 文档

新手指南与主题文档。框架是什么、当前进度、命令行细节见根目录
[README](../README.md)；术语表见 [GLOSSARY](../GLOSSARY.md)；规格书（v1 定义）
在 `.scratch/embark-v1/spec.md`；工单在 `.scratch/embark-v1/issues/`。

## 最短路径

三步：**跑起来**（构建 + 运行 demo）、**敲起来**（用命令行开关做自动验收）、
**改起来**（照模板加一个 App）。

### 1. 跑起来

Windows（MinGW + Ninja）与 Linux（apt 装 `ninja-build libsdl2-dev`）同一条路：

```sh
git submodule update --init --recursive   # 拉齐 ETL / efmt-elog / LVGL / FreeRTOS / doctest
cmake -G Ninja -B build                   # 配置（宿主 + 测试；SDL2 找不到只跳过 UI demo）
cmake --build build                       # 构建（零警告要求）
ctest --test-dir build --output-on-failure  # 单元测试（95 用例 / 835 断言）
./build/platform/host/embark_host_ui      # 宿主 UI demo（Windows: .\build\platform\host\embark_host_ui.exe）
./build/platform/host/embark_host_tour    # 系统用例：从启动看到任务切换（同上 Windows 加 .exe）
```

看到 240×320 的 LVGL 窗口（竖屏，标题 "Embark demo"，默认前台是 launcher App —— EEZ launcher 屏）就跑起来了。
关窗退出。想要"跑够帧数自己退"或验证参数，看下一步。

`embark_host_tour` 是"一条用例看完整系统"：不加参数就会自己走完
**启动 → 后台节拍 → 合成点击切前台 → App 间消息 → 再切回来 → 一次性任务跑完并回收 → 关窗收尾**，
边跑边在控制台用中文解说每一步（前台是谁、谁收到了消息、钩子跑了几次、任务池还剩几个槽），
最后打一张 10 项自检清单；全部通过退出码 0，任一项不满足退出码 2。
同一条流程的**无窗口版本**是 doctest 用例 `系统用例：从启动到任务切换走一遍`
（`tests/kernel/test_system_tour.cpp`），在 CLion 里单跑那一条即可从上往下读日志；
任务生命周期那一步的用例是 `系统用例：own task 创建 → 跑完 → 回收 → 再创建`
（`tests/kernel/test_own_task_lifecycle.cpp`）。

日志是整对象风格（issue 13）：类型自己在声明处登记怎么打，调用点直接填空，
比如启动时的 `HAL 就绪：显示 embark::hal::DisplayInfo { width = 240, height = 320, ... }`、
每个 App 的 `后台配置 embark::AppSettings { background = ..., period_ms = 100, ... }`。
加字段不用改日志行；新类型怎么登记见 [common-pitfalls.md](common-pitfalls.md) 的
"派生打印"一节（含 384 字节单行上限）。

### 2. 敲起来

宿主 UI 可执行文件的开关都是为自动验收做的（`--help` 有完整清单）：

```sh
./build/platform/host/embark_host_ui --click                     # 最短验收：launcher ⇄ clock（点 EEZ 屏按钮往返），退出码 0
./build/platform/host/embark_host_ui --frames 150 --launch --own-task
# 合验：EEZ 屏按钮往返切前台 4 次（含亮度消息 1 发 1 收）+ ticker 自己的任务 24 发 24 收，退出码 0
./build/platform/host/embark_host_ui --frames 150 --own-task    # 只验消息与 own_task
```

退出码约定：0 = 全部验收通过；2 = 某项断言没满足（日志里 `验收失败` 会说出
是哪个 App、期望什么）；1 = 启动失败。详细的开关表见根 README。

### 3. 改起来：加一个只显示一行字的 App

照着 demo 的模板走，总共五步：

1. **新建 `app/hello_app.h`**（薄壳：不 include LVGL、不持有屏对象，界面全部交给 EEZ）：

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

2. **新建 `app/hello_app.cpp`**（薄壳纪律：不建屏、不手绘；进入前台只按屏名挂自己的 EEZ 屏）：

```cpp
#include <embark/log.h>

#include "hello_app.h"

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

3. **`app/CMakeLists.txt`**：`embark_demo_apps` 的源列表加 `hello_app.cpp`：

```cmake
add_library(embark_demo_apps STATIC launcher_app.cpp demo_apps.cpp hello_app.cpp
        eez_demo_app.cpp eez_ui_bridge.cpp eez_ui_nav.cpp)
```

4. **`platform/host/ui_demo.cpp`**：`EMBARK_APP_TABLE(...)` 里加进注册表
（`LauncherApp` 必须第一位：默认前台 + `request_home()` 目标 + 主屏）：

```cpp
EMBARK_APP_TABLE(embark::demo::LauncherApp,  // 首位必须是启动器
                 embark::demo::HelloApp)     // 你的新 App
```

5. **在 EEZ Studio 画 `hello` 屏**（屏名必须等于 App 名）：不画也能编译运行——
`enter_app_screen` 找不到同名屏时保持当前屏 + 一条告警（缺屏策略 A），画好同名屏后自动生效。

6. **重新构建并跑**：

```sh
cmake --build build
./build/platform/host/embark_host_ui --frames 30 --click   # hello 在后，先验收原有 App
```

想让它被启动：在 EEZ Studio 的 launcher 屏加一个按钮，Flow SetPage 到 `hello` 屏——
屏观察者自动 `request_switch("hello")`。不需要改任何框架代码——App 只是注册表里多了一项。
想让它退后台后做点事（周期任务/自己的任务/发消息给别的 App），读 `messages-and-background.md`。

> 注意：界面文案全部在 EEZ Studio 里设置（字体在工程里配）；日志中文没问题。


### 用 EEZ Studio 画界面（手绘 UI 已退役，全程只调接口）

界面（外观、交互、切屏）全部由 EEZ Studio 负责，App 侧一行 LVGL 都不用写，
只需在 4 个钩子里调用薄桥接口（`app/eez_ui_bridge.h`，命名空间 `embark::demo`）：

1. `onCreate` → `eez_ui_bridge_ensure_init()`（幂等启动生成代码）。
2. `onEnter` / `onResume` → `eez_ui_bridge_enter_app_screen(name())`（按屏名约定挂自己的屏）。
3. `onForegroundTick` → 先 `set_var_*(...)` 推 UI 显示数据（Flow 全局变量），再 `eez_ui_bridge_tick()`。
4. 主屏（launcher）额外 `eez_ui_nav_attach(fw)`：EEZ 里 SetPage 切屏 = 框架切到同名 App。

完整流程见 [eez-ui-manual.md](eez-ui-manual.md)（用户手册）；Studio 安装/导出见
[eez-studio-guide.md](eez-studio-guide.md)。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [messages-and-background.md](messages-and-background.md) | 消息（总线 / 收件箱信封）与三种后台策略怎么用，含示例代码；`own_task` 的生命周期（入口返回 = 结束、每帧回收、运行期再创建）也在这里 |
| [hal-backend-guide.md](hal-backend-guide.md) | 怎么写一个 HAL 后端：宿主骨架（照 platform/host/）、共享层（platform/common/）与真机实现（platform/esp32/，含 IDF 坑清单） |
| [../platform/esp32/README.md](../platform/esp32/README.md) | ESP32-S3 真机端口：板级参数、构建/烧录命令、bring-up 清单、串口日志样例 |
| [eez-ui-manual.md](eez-ui-manual.md) | EEZ UI 用户手册：界面交给 EEZ、App 只调 4 个接口（薄桥 API 全集 + 命名约定 + 模板） |
| [eez-studio-guide.md](eez-studio-guide.md) | EEZ Studio 一条龙：装 Studio、建工程、导出代码入库（.eez-project） |
| [common-pitfalls.md](common-pitfalls.md) | 常见坑：ETL 定容行为、消息非聚合、保留 id、无异常/无堆、MinGW 对齐分配、日志 384 字节上限、派生打印（E_FMT_DERIVE）、宏前置条件…… |
| [adr/](adr/) | 架构决策记录：单一 UI 任务（0001）、HAL 能力粒度（0002）、零堆无异常（0003）、后台节拍与状态范式（0004）、静态槽位与任务池（0005） |
| [agents/](agents/) | 面向 agent 的仓库约定（领域模型、issue 追踪规则） |

交叉参考：内核契约测试在 `tests/kernel/`（发消息、切前台、节拍、own_task 生命周期与
任务池装配的"正确用法"都在测试里，比任何文档都新）；`.scratch/embark-v1/spec.md`
§5–§10 是接口设计与约束的来源。