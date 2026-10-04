# 14 · 系统用例：一条从启动看到任务切换的完整流程

Status: resolved
Type: task
Blocked by: 13
来源: 用户提问（"我看clion里的用例都是分开的，能不能给我一个完整的系统启动，任务切换的用例让我看看系统从启动到任务切换呢？"）

## 目标

单元测试是按契约切开的（注册表 / boot 顺序 / 切换 / 消息 / own_task 各一条），缺一条
"把整套系统装起来跑一遍"的用例。本 issue 交付两个入口，走的是同一条流程：

1. **可运行程序** `embark_host_tour`（给人看）：真平台后端（同一个 UI 任务、真 LVGL、
   真 SDL 输入、真 FreeRTOS 任务），**不加参数就完整跑一遍**，控制台逐步中文解说，
   最后打 8 项自检清单；全部通过退出码 0，任一项不满足退出码 2。
2. **无窗口 doctest 用例** `系统用例：从启动到任务切换走一遍（跟着日志读）`
   （给 CI 与读断言的时候）：纯假后端，同样的一条流程，日志同样自解释。

## 范围

- 新增 `platform/host/ui_tour.cpp`：9 步解说式流程（进程入口 → HAL → 框架 boot →
  后台节拍 → 合成点击切前台 → 亮度消息回到后台的 clock → 再切回来（onResume）→
  own_task 回流 → 关窗收尾）+ `CheckList` 自检清单（8 项：唯一 UI 任务跑完帧循环、
  后台节拍次数、切换 2 次、clock enter/resume、settings enter/resume、App 间消息、
  own_task 回流、收件箱溢出）。开关只有 `--scale / --delay / --frames / --screenshot / --help`。
- `platform/host/CMakeLists.txt`：新增目标 `embark_host_tour`（与 `embark_host_ui` 同款依赖
  与 SDL2 运行时拷贝）。它不是单元测试，不进 ctest。
- 新增 `tests/kernel/test_system_tour.cpp`：单条 doctest 用例，装配 FakeHal + FakeUiPort +
  `TourClockApp`（tick 40 ms）/ `TourSettingsApp`（默认 suspend），打印 7 个钩子的调用、
  publish 广播、post 跨任务信封、前台切换的边界语义，并用 CHECK 钉住关键结果；
  加进 `tests/CMakeLists.txt` 的 kernel 源码列表。
- 文档：根 `README.md`（新增"一条用例看完整系统"小节 + 目录表两处提及）、
  `docs/README.md`（最短路径加 `embark_host_tour`，用例数改 86 / 666）。
- 证据：`.scratch/embark-v1/evidence/14-system-tour/`（三份运行日志）。

## 验收

- `cmake --build build` 零警告；`ctest` / `embark_tests.exe` 全绿（含新增用例）。
- `.\build\platform\host\embark_host_tour.exe` 退出码 0，日志出现 8 项 `[通过]`
  与 `全流程通过`。
- `embark_host_ui` 三种跑法不受影响（`--click --switch --own-task` 退出码 0）。
- `clang-format --dry-run --Werror` 对两个新文件零不符。

## Answer

### 交付物

| 入口 | 形态 | 命令 |
| --- | --- | --- |
| `embark_host_tour` | 窗口 + 中文逐步解说 + 8 项自检清单 | `.\build\platform\host\embark_host_tour.exe` |
| doctest 系统用例 | 无窗口、纯假后端 | `.\build\tests\embark_tests.exe`（CLion 里单跑该 case） |

### 实测（本机）

- 构建：`cmake --build build` 零警告（`BUILD_EXIT=0`）。
- 单元测试：**86 用例 / 666 断言全绿**（原 85 / 629；新增 1 用例、37 断言）。
- 系统用例程序（`TOUR_EXIT=0`）：8/8 `[通过]` —— 唯一 UI 任务跑完帧循环 80、后台节拍
  clock ticks 4、前台切换 2 次、clock enter/resume 1/1、settings enter/resume 1/0、
  App 间消息 brightness 1、own_task 回流 12、收件箱溢出 0。收尾统计：刷新 52 次
  （937386 字节）、窗口 240×320、总线发布 13 条且无人接收 0、LVGL 未回收 10294 字节
  （预算 262144）、UI 任务栈余量 2045 字、FreeRTOS 任务数 3。
- 无窗口系统用例（退出码 0）：11 帧走完 ①–⑦ —— `onCreate ×2 → 前台 onEnter → 第 8 帧
  后台节拍 → publish 广播到两个 App → post 在下一帧派发 → 切换（onPause/onEnter）→
  切回（onPause/onResume）→ shutdown（前台 onPause + 两个 onExit）`。
- 回归：`embark_host_ui --frames 150 --click --switch --own-task` = `UI1_EXIT=0`。
- 证据：`evidence/14-system-tour/tour-host-run.log`、`tour-case-log.txt`、
  `host-ui-regression.log`。

### 两个入口的分工

- 给人看：`embark_host_tour` —— 真后端 + 窗口，画面与解说同时进行，适合演示与上板前自检。
- 给判定：doctest 系统用例 —— 无窗口、不依赖 SDL2，适合 CI；断言本身即验收条件。

## 备注

- 解说按**事件**播报，不按"点击后第 N 帧"：实测合成点击到切换生效有约 4 帧延迟，
  按帧号播报会打出与事实相反的结论（第一版实现就踩了这个）。
- CLion 的可运行配置由 CMake 目标自动生成（`.idea/` 不进仓库），reload CMake 后
  `embark_host_tour` 会出现在运行配置下拉框里，无需提交任何 IDE 文件。
