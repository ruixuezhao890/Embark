# Embark

[![CI](https://github.com/ruixuezhao890/Embark/actions/workflows/ci.yml/badge.svg)](https://github.com/ruixuezhao890/Embark/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)

**可移植的嵌入式应用框架**：全局只有**一个 UI 任务**，多个 App 作为「逻辑模块」共享它。

- 前台 App 拿输入焦点、渲染权与事件循环权；后台 App 按各自策略挂起，或按周期跑轻量逻辑
- **同一份源码**同时构建 **宿主（PC 仿真）** 与 **ESP32-S3** 两个目标，App 层一行不改
- 内核**零堆分配**、`-fno-exceptions -fno-rtti`、容器只用 ETL 定容版 —— 「内存够不够」是链接期问题，不是运行期问题
- 界面交给 **EEZ Studio** 导出，App 只调几个接口；改个坐标不用碰 C++

---

## 它解决什么问题

嵌入式 UI 项目通常会在三个地方翻车：

| 常见做法 | 代价 |
| --- | --- |
| 每个页面一个 FreeRTOS 任务 | 并发、栈深、锁 —— 而 LVGL 本来就不是线程安全的 |
| 业务代码直接 `lv_label_set_text()` / 直接摸寄存器 | 换块板子重写一遍 |
| 界面用 C++ 手搓像素坐标 | 改个间距要重新编译，设计师没法参与 |

Embark 的三条回答：

1. **唯一 UI 任务 + App 是逻辑模块** —— 所有 App 在同一个任务里被钩子驱动，零并发、零锁。
   重的、真并行的活走 `own_task` 逃生舱（静态池里的任务，跨任务只走消息）。
2. **HAL 的能力粒度是「芯片功能」**，不是寄存器也不是设备驱动 —— App 通过 `fw.hal()` 拿总线自己写驱动，
   于是同一份 App 在宿主机（SDL2）和真机（ST7789 + CST328）上都跑得起来。
3. **界面是资源，不是代码** —— EEZ Studio 里画屏、导出的 C 代码入库，App 只负责
   `挂屏` / `写变量` / `收切屏请求`。

---

## 30 秒看一个 App

```cpp
class ClockApp final : public App {
 public:
  const char* name() const noexcept override { return "clock"; }
  const char* title() const noexcept override { return "时钟"; }

  // 后台跑：每 100 ms 一次 onBackgroundTick（在 UI 任务里，必须轻量）
  AppSettings settings() const noexcept override {
    return AppSettings{BackgroundPolicy::tick, 100U, 0U, 0U};
  }

  void onCreate(Framework& fw) noexcept override { fw_ = &fw; }   // 装配期，恰好一次
  void onEnter() noexcept override { load_screen(); }              // 第一次成为前台
  void onForegroundTick(std::uint32_t now_ms) noexcept override;   // 前台每帧一次
  void onBackgroundTick(std::uint32_t now_ms) noexcept override;   // 后台按周期
  void onExit() noexcept override {}                               // 关机路径
 private:
  Framework* fw_ = nullptr;
};
```

完整可运行的最小例子在 [`examples/minimal/`](examples/minimal/)（不依赖 EEZ、不依赖 demo App，
一个 App + 一个 `main.cpp`，约 250 行含注释）：

```sh
cmake -G Ninja -B build && cmake --build build --target embark_example_minimal
./build/examples/minimal/embark_example_minimal --frames 90
```

---

## 快速开始

**前置**：Git、CMake ≥ 3.24、Ninja、C++17 编译器（GCC / Clang / MSVC）。
想看窗口还要 SDL2 —— Windows 装 `SDL2-devel-*-mingw`，Linux `sudo apt install libsdl2-dev`。

```sh
git clone --recursive https://github.com/ruixuezhao890/Embark.git
cd Embark
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure

./build/platform/host/embark_host_ui        # Windows: .\build\platform\host\embark_host_ui.exe
```

看到 240×320 的竖屏窗口、里面是 launcher 屏、点按钮能切到 clock 屏 —— 就算跑起来了。

> 已经 clone 过但没带子模块：`git submodule update --init --recursive`。
> 工具安装的逐步说明（含 Windows 上的踩坑）在
> [docs/guides/quickstart.md](docs/guides/quickstart.md)。

**下一步**：[加一个自己的 App](docs/guides/new-app-guide.md)（30 分钟端到端，含画 EEZ 屏）。

---

## 仓库里有什么

| 路径 | 放什么 |
| --- | --- |
| `include/embark/` | 框架公开头文件 —— **上层只依赖这里** |
| `src/` | 内核实现（Framework、Bus、AppRegistry、任务池、日志） |
| `platform/host/` | 宿主后端：SDL2 显示/输入、LVGL 端口、文件存储、FreeRTOS、UI 任务、三个入口 |
| `platform/esp32/` | ESP32-S3 后端（ESP-IDF 组件形式接入） |
| `platform/common/` | 两端共享的端口层（LVGL 端口、静态池、任务池） |
| `app/` | 自带示例 App（launcher / clock / settings）+ EEZ 生成代码 + 薄桥 |
| `examples/minimal/` | 不依赖 EEZ 的最小可运行 App —— **从这读起** |
| `tests/` | doctest 用例：`hal/` 按能力分、`kernel/` 是内核契约、`fakes/` 是 HAL 假后端 |
| `config/` | 编译期宏、`lv_conf.h` 与**全部容量上限**（单一事实来源） |
| `tools/` | 脚手架（`scaffold_app.py`）、字库生成、真机尺寸检查 |
| `docs/` | 文档三层：[docs/index.md](docs/index.md)（concepts / guides / reference）+ ADR |
| `.scratch/` | 规格书与 issue 追踪（随仓库提交，是设计决策的一手记录） |

---

## 文档

三层结构，按「你想知道什么」找入口 —— 总索引 [docs/index.md](docs/index.md)：

| 层 | 回答 | 入口 |
| --- | --- | --- |
| **concepts** | 为什么这么设计 | [分层总图 + 一帧数据流](docs/concepts/index.md) |
| **guides** | 怎么用 | [5 分钟跑起来](docs/guides/quickstart.md) → [加一个 App](docs/guides/new-app-guide.md) |
| **reference** | 具体是多少 / 叫什么 | [8 个钩子的契约表 + API 全集](docs/reference/hooks-and-api.md) |

另外：架构决策记录在 [docs/adr/](docs/adr/)（每条都写了被否决的方案），
术语表在 [GLOSSARY.md](GLOSSARY.md)（写代码、写 issue、写文档都用表里的词）。

---

## 三个宿主入口

同一个内核、同一套 App，三个不同用途的可执行文件：

| 目标 | 用途 |
| --- | --- |
| `embark_host` | 无窗口，走一遍 HAL 自检（时间 / 持久化 / 总线 / 系统） |
| `embark_host_ui` | **演示 + 自动验收**二合一：`--click` / `--launch` / `--drag` / `--eez` 各是一段脚本化故事 |
| `embark_host_tour` | 一条用例看完整系统：按 ①→⑨ 走完进程入口到关窗，17 项自检清单全过退出码 0 |
| `embark_host_user` | **你自己的程序**：干净入口 [platform/host/user_main.cpp](platform/host/user_main.cpp)，改 `EMBARK_APP_TABLE` 即得 |

```sh
./build/platform/host/embark_host_ui --launch      # 完整故事：切前台 ×4 + 跨 App 消息
./build/platform/host/embark_host_tour             # 17 项系统自检
```

---

## ESP32-S3 构建

真机后端在 `platform/esp32/`，以 ESP-IDF 5.4 工程接入（同一个仓库、同一份内核与 App 源码）：

```sh
idf.py -C platform/esp32/project -B build-esp32 set-target esp32s3
idf.py -C platform/esp32/project -B build-esp32 build      # 只编不烧
idf.py -C platform/esp32/project -B build-esp32 -p COM5 flash monitor
```

板型参数、bring-up 清单与串口日志样例见 [platform/esp32/README.md](platform/esp32/README.md)。

---

## 状态

**v0.1.0，可以用了，但还不是 1.0。** 诚实的边界：

- ✅ 内核（App 契约、编译期注册表、唯一 UI 任务、前后台切换）、消息总线、三种后台策略、own task 运行期生命周期
- ✅ 宿主与 ESP32-S3 两个后端、LVGL 8.3.11、EEZ Studio 适配（含屏生命周期回收）
- ✅ 零堆审计、CI 三个 job（宿主构建测试 / 真机构建 / clang-format）
- ⚠️ **真机上的界面观感还没确认**（屏幕方向/颜色、触摸方向）—— 需要板子到手
- ⚠️ RTC 对时（`epoch_ms` 目前返回 `unsupported`）与 SD 卡总线还没做
- ⚠️ 平台只有 host 与 esp32 两个；API 在 1.0 前仍可能微调

变更历史见 [CHANGELOG.md](CHANGELOG.md)。

---

## 参与

Issue 与 PR 都欢迎。动手前请读 [CONTRIBUTING.md](CONTRIBUTING.md) —— 里面写了构建、
代码风格、提交信息格式，以及这个仓库**特有**的几条纪律（零堆、不用异常、App 不碰平台头）。

---

## 许可

MIT，见 [LICENSE](LICENSE)。
