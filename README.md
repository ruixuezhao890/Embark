# Embark

可移植的嵌入式应用框架：**全局只有一个 UI 任务**，多个 App 作为"逻辑模块"共享它；
前台 App 拿到输入焦点、渲染权与事件循环权，后台 App 按各自策略挂起或跑轻量逻辑。
同一份源码同时构建 **宿主（PC 仿真）** 与 **ESP32-S3** 两个目标。

- 语言与约束：C++17；内核零堆分配、`-fno-exceptions -fno-rtti`、容器只用 ETL 定容版
- 硬件抽象：按"芯片功能被抽象出来给上层调用"的粒度，不暴露寄存器与引脚细节
- 日志与格式化：[efmt-elog](https://github.com/ruixuezhao890/efmt-elog)（`<middleware/...>` 形式引用）
- 容器与消息等基础设施：[ETL](https://github.com/ETLCPP/etl)（锁 20.49.0）
- UI 与图形：[LVGL](https://github.com/lvgl/lvgl)（锁 v8.3.11；宿主走 SDL2 窗口，真机走 ST7789）

## 状态

v0.1.0 进行中：构建系统、依赖与配置注入已就位；HAL 七个能力的接口、宿主的**七个后端**
（时间 / 持久化 / 日志 sink / 系统控制 / 总线 / SDL2 显示 / SDL2 输入）与测试用假后端已落地；
LVGL 8.3.11 已接入（宿主可跑出窗口、点击有响应、关窗干净退出）。
**已完成**：App 内核（App 契约 + 编译期注册表 + 唯一 UI 任务 + 前后台切换，宿主 FreeRTOS
V10.6.2 静态接入、零动态分配）、消息派发与后台节拍（Bus / MessageQueue / callback_timer，
issue 07）、三种后台策略的演示 App（issue 08：clock 用 `etl::state_chart` + 后台 tick、
settings 全挂起、ticker 用自己的任务发消息）、零堆审计（issue 09：全局 new/delete 钩子 +
内核稳态路径断言 0 次，73 用例 / 495 断言全绿）、CI 三个 job（issue 10：宿主构建测试 /
ESP32 构建 / clang-format 检查，`.github/workflows/ci.yml`）。宿主 UI 演示里切换前台、
输入焦点随之切换、后台 tick 计数与跨 App 消息都可观测。
**还没做**：ESP32-S3 后端（issue 11，等你给板型与触摸型号）——按
`.scratch/embark-v1/issues/` 里的工单继续。规格书见 `.scratch/embark-v1/spec.md`，
新手路径见 [docs/README.md](docs/README.md)。

## 宿主构建

依赖以 git submodule 引入，先拉齐：

```sh
git submodule update --init --recursive
```

配置、构建、跑测试（Windows 下同样适用，Ninja 由 CMake 自己找）：

```sh
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

跑宿主可执行文件（打印版本，并走一遍 HAL 自检：时间 / 持久化 / 总线 / 系统）：

```sh
./build/platform/host/embark_host              # Windows: .\build\platform\host\embark_host.exe
```

持久化后端默认把键值写到 exe 同目录的 `embark_host_kv.bin`（32 槽 × 92 字节 = 2944 字节）；
要先清干净就把这个文件删掉，或设 `EMBARK_HOST_STORAGE=<路径>` 换个位置。自检每次会
`boot_count` + 1，连跑两次数字应当递增 —— 这是"宿主持久化真的落到文件了"的最短证据。

## 宿主 UI 演示（SDL2 + LVGL）

需要本机有 SDL2（MinGW 发行版即可，`find_package(SDL2 CONFIG)` 找得到就编；找不到时只跳过
这个目标，内核与测试照常构建）：

```sh
./build/platform/host/embark_host_ui          # Windows: .\build\platform\host\embark_host_ui.exe
```

窗口是 320×240 的 LVGL 界面按 `--scale`（默认 2）放大显示。运行在唯一 UI 任务里
（FreeRTOS 静态任务，5 ms 一跳）：UI 端口 → 输入泵 → 循环边界的前台切换 → 消息/后台节拍 →
`lv_timer_handler()`。四个演示 App 都在 `app/`（named 空间 `embark::demo`，不含任何平台头）：

| App | 后台策略 | 演示点 |
| --- | --- | --- |
| `ClockApp` | `Tick`（100 ms） | 内部用 `etl::state_chart` 表达亮/灭状态机；后台节拍驱动状态转移并刷新界面 |
| `SettingsApp` | `Suspend` | 前台才有行为的典型设置页；「Level +1」发 `BrightnessMessage` 经总线广播，clock 收到后更新亮度标签 |
| `TickerApp` | `OwnTask`（50 ms） | 自己的任务每拍发一条 `CrossTaskMessage`，UI 任务收到后经总线回派给 App 自己，不丢不乱序 |
| `HelloApp` | `Suspend` | 最简 App 模板（只显示一行字）——「加一个 App」的起点，见 [docs/README.md](docs/README.md) |

命令行开关：

| 开关 | 作用 |
| --- | --- |
| `--frames N` | 跑满 N 帧就退出（默认 0 = 一直跑到关窗） |
| `--click [X,Y]` | 第 20 帧合成一次点击（默认点按钮中心 160,170；走 SDL 真事件队列）；settings 没被切进前台则退出码 2 |
| `--switch` | 合成两次点击验证前台切换（第 30/32 帧点「Level +1」发亮度消息，第 50/52 帧点「Back to clock」切回）；钩子序或切换次数不对则退出码 2 |
| `--own-task` | 验证 own_task 后台 App：TickerApp 发出的每条消息都必须被 UI 任务收到（不丢不乱序）；发送或收到为 0 则退出码 2 |
| `--screenshot FILE` | 最后一帧存成 BMP |
| `--quit-at N` | 第 N 帧合成关窗事件（等价于点窗口 ×，用来验收"干净退出"） |
| `--scale S` / `--delay MS` | 窗口放大倍数（默认 2）/ 每帧让出的毫秒数（默认 5） |
| `--help` | 用法 |

最短的自动验收（退出码 0 + 日志里 `clock 进入前台`）：

```sh
./build/platform/host/embark_host_ui --frames 60 --click
```

前台切换验收（退出码 0 + 日志里 `切换 2 次`、clock 的 enter/resume 计数符合
"首次 onEnter、之后 onResume"）：

```sh
./build/platform/host/embark_host_ui --frames 80 --switch
```

消息与后台任务验收（退出码 0 + 日志里 `ticker 发送 23 条 / UI 收到 23 条`、总线发布与
收件箱溢出均为 0）：

```sh
./build/platform/host/embark_host_ui --frames 150 --own-task
```

## ESP32-S3 构建

`platform/esp32/` 目前是占位（构建即报错：该后端随 issue 11 落地，等你提供
板型与触摸控制器型号）。落地后的命令（ESP-IDF 5.4 组件形式接入）：

```sh
idf.py set-target esp32s3   # 在 platform/esp32/ 下执行
idf.py build                # 只编不烧（CI 的 esp32 job 同款，issue 11 后转必需）
```

在真机后端就绪前，所有功能都在宿主上开发与验收——同一份 App 代码，换后端
不动 `app/` 与 `include/embark/`（spec §14.5 验收项，见
[docs/hal-backend-guide.md](docs/hal-backend-guide.md)）。

## 怎么加一个 App

App 是 `embark::App` 的子类，注册进**编译期静态注册表**即可，不需要改框架
（后台策略、生命周期钩子的完整说明见 [docs/messages-and-background.md](docs/messages-and-background.md)）：

1. 在 `app/` 新建 `hello_app.h` / `hello_app.cpp`（类 HelloApp；七个钩子，
   `name()` 返回唯一名字）；
2. `app/CMakeLists.txt`：`embark_demo_apps` 源列表加 `hello_app.cpp`；
3. `platform/host/ui_demo.cpp`：`EMBARK_APP_TABLE(...)` 里加
   `embark::demo::HelloApp`（放最前 = 默认前台）；
4. `cmake --build build` 重新构建，运行 demo 即可看到它。

完整可复制的五步清单（含代码）在 [docs/README.md](docs/README.md) 的
「改起来」最短路径。

## 目录

| 路径 | 放什么 |
| --- | --- |
| `include/embark/` | 框架公开头文件（上层只依赖这里，见 spec §11） |
| `src/` | 内核实现（Framework、日志、错误等） |
| `platform/host/` | 宿主后端（SDL2 显示/输入、LVGL 端口、宿主文件存储、FreeRTOS 配置与 UI 任务、演示 UI 入口） |
| `platform/esp32/` | ESP32-S3 后端（以 ESP-IDF 组件形式接入） |
| `app/` | 自带示例 App（clock/settings/ticker 三种后台策略各一 + hello 最简模板；不含平台头） |
| `tests/` | 宿主单元测试（doctest）：`tests/hal/` 按能力分文件，`tests/fakes/` 是 HAL 假后端，`tests/detail/` 是内部工具，`tests/kernel/` 是 App 注册表与 Framework 契约测试 |
| `config/` | 编译期宏、`lv_conf.h` 与固定容量上限（单一事实来源） |
| `cmake/` | 构建辅助（`middleware/` 视图生成、SDL2 探测与运行时拷贝、FreeRTOS 内核目标） |
| `third_party/` | 依赖（submodule） |
| `docs/` | 文档：新手最短路径与索引在 [docs/README.md](docs/README.md)，HAL 后端 / 消息与后台策略 / 常见坑各一篇，ADR 在 `docs/adr/` |
| `.scratch/` | 规格书与 issue 追踪（随仓库提交） |

## 许可

MIT，见 [LICENSE](LICENSE)。
