# 02 · 宿主 FreeRTOS 可用性 spike（证据留存）

一次性探针，回答 issue 02：**归档的 FreeRTOS V10.6.2 + `portable/MSVC-MingW` 端口能不能在
MinGW-w64 GCC 15.1 上编过并稳定跑**。结论是能跑通；这里留的是当时的源码与原始日志，
不是仓库骨架的一部分（骨架落在 `platform/host/`，见 issue 06）。

- kernel 只读引用归档：`E:\01_Workspace\01_Archives\lvgl_template_laste\freertos_\kernel`
- `FreeRTOSConfig.h`、`hooks.c` 是从同一个归档工程复制过来的两份文件（FreeRTOS 为 MIT 许可）

## 怎么重跑

```powershell
$mingw = "E:\mingw\x86_64-15.1.0-release-posix-seh-ucrt-rt_v12-rev0\mingw64\bin"
$env:PATH = "$mingw;$env:PATH"
# 把本目录拷到临时目录再构建（或直接在本目录建 build/，日志别留在仓库里）
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug `
      -DCMAKE_C_COMPILER="$mingw\gcc.exe" -DCMAKE_CXX_COMPILER="$mingw\g++.exe"
cmake --build build
.\build\spike.exe          # 正常路径：1000 tick 后 exit()
.\build\sleepprobe.exe     # 附带探针：量 Windows Sleep() 粒度

# 对照组（演示 vTaskEndScheduler 会挂死，需要手动结束进程）
cmake -G Ninja -B build-endsched -DCMAKE_CXX_FLAGS=-DSPIKE_END_SCHEDULER
cmake --build build-endsched ; .\build-endsched\spike.exe

# 长跑档（5000 tick）
cmake -G Ninja -B build-soak -DCMAKE_CXX_FLAGS=-DSPIKE_RUN_TICKS=5000
cmake --build build-soak ; .\build-soak\spike.exe
```

## 实测（2026-10-04，本机）

| 档位 | tick | 墙钟 | ms/tick | UI 任务循环 | 生产者/消费者 | 队列满 | heap_free | 退出码 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 默认 ×3 | 1000 | 1995 / 1989 / 1997 ms | 2.00 / 1.99 / 2.00 | 500 | 200 / 200 | 0 | 81288 | 0 |
| 长跑 | 5000 | 9983 ms | 2.00 | 2500 | 1000 / 1000 | 0 | 81288 | 0 |
| 对照组 | 1000 | 挂死（等满 12 s 被强杀） | — | — | — | — | — | — |

计数器与 tick 数严丝合缝（周期 2 tick 的 UI 循环 = tick/2，周期 5 tick 的生产者 = tick/5），
说明**任务调度与延时语义完全正常**；只有「墙钟时间」被拉长了 2 倍。

`logs/` 里是原始输出：`spike-run-repeat.log`（三次默认档）、`spike-run-endsched.log`（对照组，
可见结论打印完就卡住）、`spike-sleepprobe.log`、`embark-spike-strict.log`（严格警告档构建）。

## 两条必须记住的结论

1. **宿主 tick ≠ 墙钟：1 tick 实测 2.00 ms（标称 1 ms）**，且线性不漂移。
   机制：端口的 tick 线程用 `Sleep(portTICK_PERIOD_MS)` 逐拍睡（`port.c:171-178`），
   而 Windows 上 1 ms 定时器分辨率下 `Sleep(1)` 实际约 1.99 ms（`sleepprobe` 实测；
   不调用 `timeBeginPeriod` 时更会退化到 15.7 ms）。端口自己已经调了
   `timeBeginPeriod(wPeriodMin)`（`port.c:146-153`），所以这是它能做到的最好水平。
   → 宿主上的验收只能断言 **tick 相对量**，不能断言墙钟时长。
2. **`vTaskStartScheduler()` 永不返回；`vTaskEndScheduler()` 收尾会挂死。**
   `prvProcessSimulatedInterrupts()` 是 `for(;;)`（`port.c:393`），`vPortEndScheduler()`
   只把 `xPortRunning` 置 0（`port.c:567-570`）；此后任一 tick 会让
   `prvProcessTickInterrupt` 的 `configASSERT(xPortRunning)`（`port.c:366`，计时线程里还有
   `port.c:182`）落到 FreeRTOS 默认的 `configASSERT` 实现（禁用中断 + `for(;;)`）→ 进程挂死。
   → 宿主收尾一律 `exit()`（关窗 / Ctrl-C / 会话结束），不要实现「停调度器」语义。
