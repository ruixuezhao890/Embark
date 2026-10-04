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

看到 240×320 的 LVGL 窗口（竖屏，标题 "Embark demo"，默认前台是 clock App）就跑起来了。
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
./build/platform/host/embark_host_ui --frames 60 --click        # 最短验收：切到 settings，退出码 0
./build/platform/host/embark_host_ui --frames 150 --click --switch --own-task
# 三合一：切前台 2 次 + 亮度消息 1 发 1 收 + ticker 自己的任务 24 发 24 收，退出码 0
./build/platform/host/embark_host_ui --frames 150 --own-task   # 只验消息与 own_task
```

退出码约定：0 = 全部验收通过；2 = 某项断言没满足（日志里 `验收失败` 会说出
是哪个 App、期望什么）；1 = 启动失败。详细的开关表见根 README。

### 3. 改起来：加一个只显示一行字的 App

照着 demo 的模板走，总共五步：

1. **新建 `app/hello_app.h`**：

```cpp
#ifndef EMBARK_APP_HELLO_APP_H
#define EMBARK_APP_HELLO_APP_H

#include <lvgl.h>
#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class HelloApp final : public App {
 public:
  HelloApp() noexcept = default;
  [[nodiscard]] const char* name() const override { return "hello"; }
  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

 private:
  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
};

}  // namespace embark::demo

#endif
```

2. **新建 `app/hello_app.cpp`**（生命周期纪律与 demo 完全一致：屏自己建、
自己装，切换只发请求）：

```cpp
#include <embark/log.h>
#include "hello_app.h"

namespace embark::demo {

void HelloApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate", name());
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x12241f), 0);
  lv_obj_t* label = lv_label_create(screen_);
  lv_label_set_text(label, "Hello, Embark");
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void HelloApp::onEnter() { lv_scr_load(screen_); }
void HelloApp::onPause() {}
void HelloApp::onResume() { lv_scr_load(screen_); }
void HelloApp::onExit() {}

}  // namespace embark::demo
```

3. **`app/CMakeLists.txt`**：`embark_demo_apps` 的源列表加 `hello_app.cpp`：

```cmake
add_library(embark_demo_apps STATIC demo_apps.cpp hello_app.cpp)
```

4. **`platform/host/ui_demo.cpp`**：`EMBARK_APP_TABLE(...)` 里加进注册表
（注册顺序即默认前台顺序，想让 hello 当默认前台就放最前）：

```cpp
EMBARK_APP_TABLE(embark::demo::ClockApp, embark::demo::SettingsApp,
                 embark::demo::TickerApp, embark::demo::HelloApp)
```

5. **重新构建并跑**：

```sh
cmake --build build
./build/platform/host/embark_host_ui --frames 30 --click   # hello 在后，先验收原有三 App
```

想让它当前台，把 `HelloApp` 放到 `EMBARK_APP_TABLE` 第一个参数即可。
不需要改任何框架代码——App 只是注册表里多了一项。想让它退后台后做点事
（周期任务/自己的任务/发消息给别的 App），读 `messages-and-background.md`。

> 注意：界面文案用英文（LVGL 内置字体无中文字形，见 common-pitfalls）；
> 日志中文没问题。

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [messages-and-background.md](messages-and-background.md) | 消息（总线 / 收件箱信封）与三种后台策略怎么用，含示例代码；`own_task` 的生命周期（入口返回 = 结束、每帧回收、运行期再创建）也在这里 |
| [hal-backend-guide.md](hal-backend-guide.md) | 怎么写一个 HAL 后端：宿主骨架（照 platform/host/）、共享层（platform/common/）与真机实现（platform/esp32/，含 IDF 坑清单） |
| [../platform/esp32/README.md](../platform/esp32/README.md) | ESP32-S3 真机端口：板级参数、构建/烧录命令、bring-up 清单、串口日志样例 |
| [common-pitfalls.md](common-pitfalls.md) | 常见坑：ETL 定容行为、消息非聚合、保留 id、无异常/无堆、MinGW 对齐分配、日志 384 字节上限、派生打印（E_FMT_DERIVE）、宏前置条件…… |
| [adr/](adr/) | 架构决策记录：单一 UI 任务（0001）、HAL 能力粒度（0002）、零堆无异常（0003）、后台节拍与状态范式（0004）、静态槽位与任务池（0005） |
| [agents/](agents/) | 面向 agent 的仓库约定（领域模型、issue 追踪规则） |

交叉参考：内核契约测试在 `tests/kernel/`（发消息、切前台、节拍、own_task 生命周期与
任务池装配的"正确用法"都在测试里，比任何文档都新）；`.scratch/embark-v1/spec.md`
§5–§10 是接口设计与约束的来源。