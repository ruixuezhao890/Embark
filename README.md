# Embark

可移植的嵌入式应用框架：**全局只有一个 UI 任务**，多个 App 作为"逻辑模块"共享它；
前台 App 拿到输入焦点、渲染权与事件循环权，后台 App 按各自策略挂起或跑轻量逻辑。
同一份源码同时构建 **宿主（PC 仿真）** 与 **ESP32-S3** 两个目标。

- 语言与约束：C++17；内核零堆分配、`-fno-exceptions -fno-rtti`、容器只用 ETL 定容版
- 硬件抽象：按"芯片功能被抽象出来给上层调用"的粒度，不暴露寄存器与引脚细节
- 日志与格式化：[efmt-elog](https://github.com/ruixuezhao890/efmt-elog)（`<middleware/...>` 形式引用）
  —— 类型自己在声明处登记打印方式（`E_FMT_DERIVE`），调用点填整对象，见[日志](#日志)
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
内核稳态路径断言 0 次）、CI 三个 job（issue 10：宿主构建测试 / ESP32 构建 / clang-format
检查，`.github/workflows/ci.yml`）、**ESP32-S3 真机后端**（issue 11：ST7789 + CST328 的
七种能力实现、静态池 LVGL 堆、共享的 `platform/common` 端口层、IDF 5.4 真构建）、
**整对象日志**（issue 13：`Error` 等枚举/结构体在声明处登记 `E_FMT_DERIVE`，
撤掉手写 `to_string`，启动日志直接打印 HAL 信息、App 后台配置与跨任务信封）、
**运行期任务生命周期**（issue 15：own_task 走平台无关的 `PooledTaskSpawner<Kernel>` ——
静态池分槽 + `xTaskCreateStatic*`，任务入口返回 = 结束，框架每帧回收槽位，`spawn_own_task()`
可在运行期再创建；失败点仍是唯一且确定的 `no_space`）。
宿主 UI 演示里切换前台、输入焦点随之切换、后台 tick 计数与跨 App 消息都可观测。
**还没做**：真机上的界面观感确认（issue 11 的验收项 ③⑤：屏幕方向/颜色、触摸方向/触点）
——需要板子到手；另有 RTC 对时（`epoch_ms` 目前 `unsupported`）与 SD 卡总线。
规格书见 `.scratch/embark-v1/spec.md`，新手路径见 [docs/README.md](docs/README.md)。

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

窗口默认就是 240×320（竖屏，与 ST7789 面板原生方向一致）：面板像素与屏幕像素 1:1，
不缩放、不取样，所以画面不糊
（`--scale N`，N ≥ 2 时是整数倍最近邻放大，同样不模糊）。运行在唯一 UI 任务里
（FreeRTOS 静态任务，5 ms 一跳）：UI 端口 → 输入泵 → 循环边界的前台切换 → 消息/后台节拍 →
`lv_timer_handler()`。四个演示 App 都在 `app/`（named 空间 `embark::demo`，不含任何平台头）：

| App | 后台策略 | 演示点 |
| --- | --- | --- |
| `ClockApp` | `Tick`（100 ms） | 内部用 `etl::state_chart` 表达亮/灭状态机；后台节拍驱动状态转移并刷新界面 |
| `SettingsApp` | `Suspend` | 前台才有行为的典型设置页；「Level +1」发 `BrightnessMessage` 经总线广播，clock 收到后更新亮度标签 |
| `TickerApp` | `OwnTask`（50 ms） | 自己的任务每拍发一条 `CrossTaskMessage`，UI 任务收到后经总线回派给 App 自己，不丢不乱序 |
| `HelloApp` | `Suspend` | 最简 App 模板（只显示一行字）——「加一个 App」的起点，见 [docs/README.md](docs/README.md) |
| `JobApp` | `OwnTask`（`period_ms = 0`） | 一次性任务：入口跑一轮就返回，槽位被框架回收后可再创建。只挂在系统用例 `embark_host_tour` 的注册表里（宿主演示 `embark_host_ui` 是启动器 + 上面 4 个 = 5 个；真机固件仍是那 4 个 demo App） |

命令行开关：

| 开关 | 作用 |
| --- | --- |
| `--launch` | 完整故事：合成拖动（1px ≈ 0.1 槽）→ 弹簧收敛 → 点选中槽启动 clock → settings「Level +1」→ 点导航壳返回键回启动器；前台切换恰好 3 次、钩子计数/亮度/返回键请求不对则退出码 2 |
| `--click [X,Y]` | 三段合成点击：第 20 帧点非选中槽（156,196）转正、第 64 帧点选中槽（120,188）启动、第 90 帧探测默认点 120,220（`--click 120,265` 可换坐标）；前台切换 2 次，settings 没进前台则退出码 2 |
| `--drag X1,Y1,X2,Y2` | 第 15 帧按下、逐帧插值拖动到 (X2,Y2)、第 23 帧松开；断言弹簧收敛且选中槽 == 预期（1px ≈ 0.1 槽），不对则退出码 2 |
| `--own-task` | 验证 own_task 后台 App：TickerApp 发出的每条消息都必须被 UI 任务收到（不丢不乱序）；发送或收到为 0 则退出码 2 |
| `--screenshot FILE` | 最后一帧存成 BMP |
| `--quit-at N` | 第 N 帧合成关窗事件（等价于点窗口 ×，用来验收"干净退出"） |
| `--scale S` / `--delay MS` | 窗口放大倍数（默认 1 = 240×320 1:1，不糊）/ 每帧让出的毫秒数（默认 5） |
| `--frames N` | 跑满 N 帧就退出（默认按模式：launch 115 / click 110 / drag 80 / own-task 80，否则 0 = 一直跑到关窗） |
| `--help` | 用法 |

最短的自动验收（退出码 0 + 日志里 `前台切换完成` 出现 2 次：点非选中槽转正 → 点选中槽启动 clock → 探测点落在 clock 屏的按钮上）：

```sh
./build/platform/host/embark_host_ui --click
```

启动器 + 前台切换验收（退出码 0 + 日志里 `前台切换完成` 3 次：启动器 → clock → settings → 回启动器；enter/resume 计数符合
"首次 onEnter、回主屏 onResume"）：

```sh
./build/platform/host/embark_host_ui --launch
```

消息与后台任务验收（退出码 0 + 日志里 `ticker 发送 23 条 / UI 收到 23 条`、总线发布与
收件箱溢出均为 0）：

```sh
./build/platform/host/embark_host_ui --own-task
```

### 一条用例看完整系统：`embark_host_tour`

单元测试是按契约切开的（注册表、boot 顺序、切换、消息、own_task 各一条），要看
"系统装起来是什么样"，用这个目标 —— **不加参数就完整跑一遍**：

```sh
./build/platform/host/embark_host_tour     # Windows: .\build\platform\host\embark_host_tour.exe
```

它在真平台后端上（同一个 UI 任务、真 LVGL、真输入、真 FreeRTOS 任务）按 10 步走完
**进程入口 → HAL → 框架 boot（启动器主屏登场）→ 拖动换位 + 弹簧收敛 → 点选中槽启动 clock → 后台节拍
→ 合成点击切到 settings → 「Level +1」亮度消息回到后台的 clock → 点导航壳返回键回启动器（onResume）
→ own_task 回流 → 一次性任务跑完并回收（再创建一次）→ 关窗收尾**，每一步都用中文解说发生了什么
（前台是谁、钩子跑了几次、消息第几帧到达、池里还剩几个槽），最后打一张 14 项自检清单：全部通过退出码 0，
任一项不满足退出码 2（日志里 `[失败]` 会说出期望值与实测值）。开关只有
`--scale / --delay / --frames / --screenshot / --help`（`--help` 有清单）。

同一条流程还有**无窗口版本**（纯假后端，适合放在 CI 或想读断言的时候）：
doctest 用例 `系统用例：从启动到任务切换走一遍（跟着日志读）`，源码在
`tests/kernel/test_system_tour.cpp`，在 CLion 里单跑那一条即可从上往下读日志。
任务生命周期那一步的对应用例是 `系统用例：own task 创建 → 跑完 → 回收 → 再创建（跟着日志读）`
（`tests/kernel/test_own_task_lifecycle.cpp`）；池本身（槽满 `no_space`、归还后可复用、失败回滚）
的契约用例在 `tests/kernel/test_pooled_task_spawner.cpp`。

## ESP32-S3 构建

真机后端在 `platform/esp32/`，以 ESP-IDF 5.4 工程的形式接入（同一个仓库、
同一份内核与 App 源码；`platform/esp32/project/components/embark` 只是一个
"把源码喂给 IDF" 的组件壳）。本机 IDF 5.4 上的命令：

```sh
idf.py -C platform/esp32/project -B build-esp32 set-target esp32s3
idf.py -C platform/esp32/project -B build-esp32 build      # 只编不烧
idf.py -C platform/esp32/project -B build-esp32 -p COM5 flash monitor
```

构建目录 `build-esp32/` 由 `.gitignore` 覆盖；`sdkconfig` 是 `idf.py` 在
`platform/esp32/project/` 下生成的，也不进仓库（要改默认值就改
`project/sdkconfig.defaults`，注意那两个配置文件必须保持纯 ASCII）。

板型参数、bring-up 清单（屏幕方向/颜色、触摸轴、背光）与串口日志样例见
[platform/esp32/README.md](platform/esp32/README.md)；两块新硬件（屏幕与触摸）
的"上板才能确认"部分也是 issue 11 的验收项。

宿主侧想顺带看一眼门面：`cmake -S . -B build -DEMBARK_BUILD_ESP32=ON` 会加两个
自定义目标（打印构建命令 / 直接调 `idf.py`），缺 IDF 时只是提示，不失败。

在宿主上开发 App 的流程不变 —— 同一份 App 代码，换后端不动 `app/` 与 `include/embark/`
（spec §14.5 验收项，见 [docs/hal-backend-guide.md](docs/hal-backend-guide.md)）。

## 怎么加一个 App

App 是 `embark::App` 的子类，注册进**编译期静态注册表**即可，不需要改框架
（后台策略、生命周期钩子的完整说明见 [docs/messages-and-background.md](docs/messages-and-background.md)）：

1. 在 `app/` 新建 `hello_app.h` / `hello_app.cpp`（类 HelloApp；七个钩子，
   `name()` 返回唯一名字）；
2. `app/CMakeLists.txt`：`embark_demo_apps` 源列表加 `hello_app.cpp`；
3. `platform/host/ui_demo.cpp`：`EMBARK_APP_TABLE(...)` 里加
   `embark::demo::HelloApp`（放在 `LauncherApp` 之后 —— 首位必须是启动器：默认前台 + `request_home()` 的目标）；
4. `cmake --build build` 重新构建，运行 demo 即可看到它。

完整可复制的五步清单（含代码）在 [docs/README.md](docs/README.md) 的
「改起来」最短路径。

## 日志

类型自己在声明处登记打印方式（efmt 的 `E_FMT_DERIVE` / `E_FMT_DERIVE_ENUM`），
调用点只填空、不再手打字段 —— 加字段不用改日志行：

```cpp
ELOG_INFO("HAL 就绪：显示 {}", display_info);
// [info] [ui_demo.cpp:251 ui_main] 框架就绪：5 个 App，默认前台 launcher（剩余 3 槽位）
//   { width = 240, height = 320, format = embark::hal::PixelFormat::rgb565, stride_bytes = 480 }
```

两条硬规矩（细节与出处见 [docs/common-pitfalls.md](docs/common-pitfalls.md)）：

- 单条日志上限 `ELOG_MAX_RECORD_SIZE`（默认 **384** 字节，含前缀），放不下是
  **整行丢弃**（不截断、不报错）⇒ 一条日志只放一个整对象，长对象拆两条；
- 派生输出里 **1 字节整型成员会被当字符打**（上游 efmt 已知问题，0 会写出 NUL
  截断整行）⇒ 框架里会进日志的字段一律 2 字节起（`AppId`、`AppSettings::task_priority`、
  `InputEvent::key` 都是 `std::uint16_t`）。

新类型想进日志：枚举写 `E_FMT_DERIVE_ENUM(enum class E : ... { ... });`、
结构体写 `E_FMT_DERIVE(struct S { ... });`（一行一个字段），类里有基类/构造函数时
在类型体内写 `E_FMT_FIELDS(a, b);`。要 `const char*` 的出口（`fprintf`、
`embark::fatal`）用 `char text[24]; embark::error_text(text, error);`。

## 目录

| 路径 | 放什么 |
| --- | --- |
| `include/embark/` | 框架公开头文件（上层只依赖这里，见 spec §11） |
| `src/` | 内核实现（Framework、日志、错误等） |
| `platform/host/` | 宿主后端（SDL2 显示/输入、LVGL 端口、宿主文件存储、FreeRTOS 配置与 UI 任务、演示 UI 入口 `ui_demo.cpp`、系统用例入口 `ui_tour.cpp`） |
| `platform/esp32/` | ESP32-S3 后端（以 ESP-IDF 组件形式接入） |
| `app/` | 自带示例 App（clock/settings/ticker 三种后台策略各一 + hello 最简模板；不含平台头） |
| `tests/` | 宿主单元测试（doctest）：`tests/hal/` 按能力分文件，`tests/fakes/` 是 HAL 假后端，`tests/detail/` 是内部工具，`tests/kernel/` 是 App 注册表与 Framework 契约测试（含无窗口的系统用例 `test_system_tour.cpp`） |
| `config/` | 编译期宏、`lv_conf.h` 与固定容量上限（单一事实来源） |
| `cmake/` | 构建辅助（`middleware/` 视图生成、SDL2 探测与运行时拷贝、FreeRTOS 内核目标） |
| `third_party/` | 依赖（submodule） |
| `docs/` | 文档：新手最短路径与索引在 [docs/README.md](docs/README.md)，HAL 后端 / 消息与后台策略 / 常见坑各一篇，ADR 在 `docs/adr/` |
| `.scratch/` | 规格书与 issue 追踪（随仓库提交） |

## 许可

MIT，见 [LICENSE](LICENSE)。
