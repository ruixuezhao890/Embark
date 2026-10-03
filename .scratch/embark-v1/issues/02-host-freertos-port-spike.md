# 02 · 宿主 FreeRTOS（Windows port）可用性 spike

Status: resolved
Type: prototype
Blocked by: —

## 目标

验证宿主能不能用 FreeRTOS（kernel + Windows port）在 **MinGW-w64 GCC 15.1** 上编过并稳定跑任务 —— 这是 spec §3「两端任务模型一致」的前提。

## 范围

- 从归档工程 `E:\01_Workspace\01_Archives\lvgl_template_laste` 取 FreeRTOS kernel 与 Windows port（**只读，不改归档**）。
- 写一个**独立 spike**（不并入仓库骨架）：建 2 个任务、`vTaskDelay`、串口/stdout 打印心跳、跑够 1000 跳。
- CMake + Ninja + GCC 15.1；记录所需源码清单、CMake 片段、编译/链接坑（`portmacro.h` 的 naked 函数、`win32` 端口对 Win32 API 的依赖、栈对齐、需要链接的库）。

## 验收

- 跑通：给出可直接搬进 `platform/host/` 的源码清单 + CMake 片段 + 一份「踩坑记录」。
- 跑不通：给出「最小 win32 port」的改造方案与预估工作量，并明确替代路径（例如宿主用 `std::thread` 实现同一套 `embark` 任务接口，两端行为差异只落在平台层）。

## 备注

这是风险验证，产出直接决定 issue 06 的实现路径。

## Answer

**结论：跑通了**（验收走「跑通」分支，「最小 win32 port 改造方案」不再需要）。归档的 FreeRTOS **V10.6.2**（`kernel/History.txt:3`）+ `portable/MSVC-MingW/port.c` 在 MinGW-w64 GCC 15.1 上编得过、跑得稳：1 个 UI 任务 + 1 个逻辑任务 + 跨任务队列 + `vTaskDelay`，1000 tick 与 5000 tick 两档全绿，退出码 0，默认与 `-Wall -Wextra -Wpedantic` 两种告警档都编过。

证据（源码 + 原始日志 + 复跑命令）留在 `.scratch/embark-v1/spikes/02-host-freertos/`（含 `README.md`），不进仓库骨架。

必须写进 spec 的三条硬结论：

1. **宿主 tick ≠ 墙钟：1 tick = 2.00 ms（标称 1 ms），比例 1.99–2.00、线性不漂移**（5000 tick = 9.983 s）。
2. **`vTaskStartScheduler()` 永不返回；用 `vTaskEndScheduler()` 收尾会挂死**（对照组实测等满 12 s 后强杀）。宿主收尾一律走 `exit()`。
3. 调度语义本身完全正常：计数器与 tick 数严丝合缝（UI 循环周期 2 tick → 1000 tick 跑 500 次；生产者周期 5 tick → 200 条），队列零溢出、双端计数一致。

### 1. 可直接搬进 `platform/host/` 的清单

源码（除 hooks 外全部只读引用归档 kernel）：

| 来源 | 文件 |
| --- | --- |
| `${FREERTOS_DIR}` | `tasks.c`、`queue.c`、`list.c`、`timers.c`、`event_groups.c`、`stream_buffer.c` |
| `${FREERTOS_DIR}/portable/MSVC-MingW` | `port.c` |
| `${FREERTOS_DIR}/portable/MemMang` | `heap_4.c` |
| 我们自写 | `hooks.c` 等价物：`vApplicationStackOverflowHook` + （`configSUPPORT_STATIC_ALLOCATION==1` 时）`vApplicationGetIdleTaskMemory` / `vApplicationGetTimerTaskMemory` |

include 目录：`${FREERTOS_DIR}/include`、`${FREERTOS_DIR}/portable/MSVC-MingW`、放 `FreeRTOSConfig.h` 的目录（我们建议收进 `config/freertos/`）。

CMake 片段（spike 里实测可用，注意 `winmm`）：

```cmake
add_library(freertos_kernel STATIC
    ${FREERTOS_DIR}/tasks.c ${FREERTOS_DIR}/queue.c ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c ${FREERTOS_DIR}/event_groups.c ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/MSVC-MingW/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks.c)
target_include_directories(freertos_kernel PUBLIC
    ${FREERTOS_DIR}/include ${FREERTOS_DIR}/portable/MSVC-MingW
    ${CMAKE_CURRENT_SOURCE_DIR})          # 放 FreeRTOSConfig.h 的目录
target_compile_features(freertos_kernel PRIVATE c_std_11)
target_link_libraries(freertos_kernel PUBLIC winmm)   # GCC 分支下 port.c #include "mmsystem.h"
```

应用侧：C++17 + `-fno-exceptions -fno-rtti` 与 kernel 混编无障碍。

`FreeRTOSConfig.h` 非默认项（相对 FreeRTOS 默认值）：

- **`configTICK_TYPE_WIDTH_IN_BITS = TICK_TYPE_WIDTH_32_BITS`** —— Windows 模拟器端口必需；
- `configTICK_RATE_HZ 1000`、`configMAX_PRIORITIES 32`、`configTOTAL_HEAP_SIZE 128 KB`、heap_4；
- `configSUPPORT_STATIC_ALLOCATION 1`（idle/timer 任务内存走上面两个回调）、`configUSE_TIMERS 1`；
- **`configCHECK_FOR_STACK_OVERFLOW 2`** —— 必须提供 `vApplicationStackOverflowHook`，否则链接失败；
- `configUSE_MALLOC_FAILED_HOOK 0`、`configUSE_IDLE_HOOK 0`（无需对应钩子）；`configMAX_SIMULATED_INTERRUPTS 8`、`configKERNEL_INTERRUPT_PRIORITY (7<<5)`、`configMAX_SYSCALL_INTERRUPT_PRIORITY (5<<5)` 供模拟中断用。

### 2. 实测数据（2026-10-04，本机 GCC 15.1 / CMake 4.0 / Ninja）

| 档位 | tick | 墙钟 | ms/tick | UI 循环 | 生产/消费 | 队列满 | heap_free | 退出码 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 默认 #1 | 1000 | 1995 ms | 2.00 | 500 | 200 / 200 | 0 | 81288 | 0 |
| 默认 #2 | 1000 | 1989 ms | 1.99 | 500 | 200 / 200 | 0 | 81288 | 0 |
| 默认 #3 | 1000 | 1997 ms | 2.00 | 500 | 200 / 200 | 0 | 81288 | 0 |
| 长跑 | 5000 | 9983 ms | 2.00 | 2500 | 1000 / 1000 | 0 | 81288 | 0 |
| `SPIKE_END_SCHEDULER` 对照组 | 1000 | 挂死（12 s 后强杀） | — | — | — | — | — | — |

heap_4 的 128 KB 里，调度器启动前后 `xPortGetFreeHeapSize()` 都是 81288 字节（idle/timer 任务吃的是静态缓冲）。

### 3. 踩坑记录

1. **tick 频率只有标称的一半** —— 机制在 `port.c:171-178`：tick 线程逐拍 `Sleep(portTICK_PERIOD_MS)`，不是绝对时间轴。Windows 上 1 ms 定时器分辨率下 `Sleep(1)` 实测 **1.99 ms**（`sleepprobe`：默认分辨率下 `Sleep(1)/Sleep(2)/Sleep(10)` 全部 ≈ **15.68 ms**；`timeBeginPeriod(1)` 后退化消失，`Sleep(1)=1.99 ms`、`Sleep(2)=2.92 ms`）。端口自己已经调了 `timeBeginPeriod(xTimeCaps.wPeriodMin)`（`port.c:146-153`，退出时 `prvEndProcess` 配对 `timeEndPeriod`，`port.c:209-223`），所以这已是它能做到的最好水平。**对策**：宿主验收只断言 tick 相对量，绝不拿墙钟时长当判据；需要墙钟的用例给宿主单独放宽系数。
2. **`vTaskEndScheduler()` 收尾会挂死** —— `prvProcessSimulatedInterrupts()` 是 `for(;;)`（`port.c:393`），`vPortEndScheduler()` 只把 `xPortRunning` 置 0（`port.c:567-570`），main 线程照旧停在循环里；此后任一 tick 命中 `prvProcessTickInterrupt` 的 `configASSERT(xPortRunning)`（`port.c:366`，tick 线程里还有一处 `port.c:182`），而 `FreeRTOSConfig.h` 没定义 `configASSERT`，落到 FreeRTOS 默认实现（禁中断 + `for(;;)`）→ 进程假死。**对策**：宿主收尾 = `exit()`/关窗，不实现「停调度器」语义；`vTaskEndScheduler` 不要出现在平台层。
3. **多核前提 + 进程优先级**：端口拒绝单核主机（`port.c:270-274` 打印后返回 `pdFAIL`），并把进程提到 `REALTIME_PRIORITY_CLASS`（`port.c:281`，失败只打印不中断），同时把所有线程钉在 0 号核（`SetThreadAffinityMask(..., 0x01)`）。
4. **警告纪律**：整库在 `-Wall -Wextra -Wpedantic` 下只有 **2 条**上游警告 —— `queue.c:489:48`（`-Wtype-limits`）与 `port.c:249:63`（`-Wcast-function-type`，`CreateThread` 的函数指针转换）。我们自己的 `main.cpp`/`sleepprobe.cpp` 在同样告警档下零警告。要零警告就这两处定点抑制，别整段 `-w`（会连真问题一起吃掉）。
5. 归档的 `kernel/CMakeLists.txt` 是上游 CMake（要求先有 `freertos_config` INTERFACE 目标、`FREERTOS_PORT` 选 `MSVC_MINGW`/`GCC_POSIX`），归档工程自己也没用它；我们手写 STATIC 库更直白，也便于把第三方告警隔离。
6. 中文 stdout 经 PowerShell `Start-Process -RedirectStandardOutput` 读回会乱码（PS 5.1 按 GBK 解码 UTF-8 的老问题），用 `cmd /c "... > file"` 再用 read 工具看即可 —— 是读法问题，不是程序问题。

### 4. 对 spec 的影响（建议，落地到 §3/§9/§13 与 issue 06）

- 宿主「任务模型一致」这句话成立，但**时间轴不一致**：spec 要明确写「host tick 允许约 2× 拉伸，宿主验收以 tick 相对量为准」。
- 宿主生命周期 = 进程生命周期；**没有「优雅停调度器」这一步**（对应 §9 的生命周期描述要加一句）。
- issue 06 可直接按本节清单实现 `platform/host/`：kernel 从归档 vendor 到 `third_party/freertos/`（或 `platform/host/freertos/`），`FreeRTOSConfig.h` 收进 `config/`，hooks 由我们写。
- 遗留小项（不阻塞）：vendor 进仓库后要不要顺手改掉上面两条上游警告、以及宿主 tick 拉伸是否需要在日志里显式提示。
