# 22 · own task 里一条 ELOG 要 ~1.9 KB 栈：efmt 的 std::ostringstream 兜底撑爆自己的 TCB

Status: resolved（缺陷本身；结构性收口见「未做的收口」）
Type: task
Blocked by: 21
来源: 真机 bring-up（issue 21 修好后仍炸：Core 0 LoadProhibited，回溯落在 efmt 的 stream_formatter）

## 现象（证据）

issue 21 把单位修对之后 UI 任务的 16 KB 栈稳了，但 TickerApp 的后台任务（own task，钉在 core 0）
继续炸，而且炸点是内核的 SysTick 而不是它自己的代码：

    [main.cpp:106 app_main] … UI 任务栈 16384 字节，周期 5 ms
    [demo_apps.cpp:233 onCreate] App ticker onCreate（own_task，周期 50 ms，栈 256 字，优先级 4）
    [main.cpp:68 ui_main] 框架就绪：4 个 App，默认前台 clock；LVGL 8.3.11，绘制缓冲 40 行，LVGL 静态池 49120 字节
    Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.
    EXCVADDR: 0xf09ffaa0
    Backtrace: 0x4037efe3:0x3fc981e0 0x4037e29f:0x3fc98200 0x4037e321:0x3fc98220 0x403776b5:0x3fc98240
               0x4200d310:0x3fc9a960 0x4200d321:0x3fc9aa80 …

addr2line 映射（evidence/22-own-task-stack/addr2line-mapping.txt）：

    0x4037efe6: xTaskIncrementTick at …/FreeRTOS-Kernel/tasks.c:3378            ← SysTick 侧第 4 帧
    0x4037e64a: prvSelectHighestPriorityTaskSMP at …/tasks.c:3622               ← 罕见变体：读忙队列
    0x4037f0ba: vTaskSwitchContext at …/tasks.c:3712
    0x4200d310: std::__cxx11::basic_string<…>::~basic_string() at …/bits/basic_string.h:809
      (inlined by) e_fmt::detail::stream_formatter<unsigned short>::format(…) at efmt/core/format_traits.hpp:347
    0x4200d321: e_fmt::detail::default_formatter<unsigned short, void>::format(…)
      (inlined by) derive_write<unsigned short>(…) at efmt/core/format_derive.hpp:2427

读法：panic 发生在 xTaskIncrementTick() 里
listCURRENT_LIST_LENGTH(&pxReadyTasksLists[pxCurrentTCBs[0]->uxPriority])（PC 0x4037efe6，IA32 l32i.n a8, a8, 0）
—— 是**受害现场**，不是肇事者：那个任务把自己的 uxPriority 踩成了垃圾（Core 1 变体读到的正是
0xa5a5a5a5，FreeRTOS 的栈填充字节），就绪队列索引随之越界。肇事者在回溯的下半段：own task 栈上的
e_fmt::detail::stream_formatter<unsigned short>。TickerApp::onBackgroundTick() 打的那条整对象 ELOG 里
CrossTaskMessage::from_app 是 AppId = std::uint16_t，efmt 没有它的原生格式化器，
EFMT_ENABLE_HOSTED（ESP-IDF 上默认 1）⇒ std::ostringstream 兜底（efmt/core/format_traits.hpp:343
std::ostringstream oss;），一个 ostringstream 在 xtensa 上要 ~1.9 KB 栈。
任务只有 256 字 → 2048 B（issue 21 修好后是真的 2048 B），一条日志就踩穿自己的 StaticTask_t。

实测（临时栈水位探针，已删除；数据留档 evidence/22-own-task-stack/stack-high-water.txt）：

    TickerApp 栈            第一条 ELOG 之前      之后            结论
    256 字 = 2048 B         hwm=3280… 直接 panic   无              溢出，踩 TCB
    512 字 = 4096 B         hwm=3280              hwm=1344        单条日志峰值 ≈ 2752 B，余 1344 B

## 修复

- app/demo_apps.h:175-180：TickerApp::settings() 的 task_stack_words 256 → **512**（4096 B，量出来的最低值），
  注释里写明是实测值而不是拍的。
- app/demo_apps.cpp:251-262：onBackgroundTick() 上留一条**栈预算警告**注释（issue 22）：这条日志实测要
  ~1.9 KB 栈、512 字是下限、调小 = 踩穿自己的 TCB + core 0 的 SysTick 里 LoadProhibited。
- app/demo_apps.cpp:230-236：TickerApp::onCreate() 不再硬编码「栈 256 字」，改成读 settings() 打印
  period_ms / task_stack_words / task_priority —— 不然日志会说谎（改了默认值、日志还写旧值）。
- docs/common-pitfalls.md：把「elog 的开销主要是 log_at() 里 385 字节的 char record[384 + 1]」这条改写成
  「385 B 是小头，**大头是 efmt 的 std::ostringstream 兜底 ≈ 1936 字节**」，并立规矩：
  own task 里不要打没有原生格式化器的类型（典型：std::uint16_t）；任何会打日志的 own task 给
  task_stack_words >= 384（≥3 KB），4 KB（512 字）是保底线。

## 验收

- 干净启动 20 s（evidence/22-own-task-stack/post-fix-clean-run.txt，COM3 115200，122637 字节 / 908 行）：
  DIAG 0 次、Guru / panic / abort / stack overflow 各 0 次；ticker 后台任务 377 条、
  UI 收到 ticker 368 条、心跳 1 条（UI 栈余 11612 字节）、框架就绪 1 条。
- 探针已从源码树删除：在 app/ docs/ platform/ include/ src/ tests/ config/ 等源码目录 grep TEMP DIAG | EMBARK_TICK
  | namespace diag | DIAG tick | tick_diag 零命中（commit 0f33a10 之前最后一次全仓源码 grep 退出码 1）。
  注意本 issue 的 evidence/22-own-task-stack/stack-high-water.txt 里仍有 `DIAG tick N pre/post hwm=` 原文 ——
  那是当时的原始抓包，故意留证。
- 三个 demo 之外不扩大：JobApp 的 256 字栈**没动** —— 它不在真机 App 表里
  （platform/esp32/project/main/main.cpp:39-40 是 EMBARK_APP_TABLE(ClockApp, SettingsApp, TickerApp, HelloApp)），
  真机日志里没有 job 后台任务 这一行，属于宿主 ui_tour 专用。

## 未做的收口（留给后续 issue，如果需要）

现在靠"给够栈 + 注释警告"活着，编译器不会拦。真正的结构性收口有两条，都不便宜：

1. 真机侧关掉 EFMT_ENABLE_HOSTED：没有原生格式化器的类型会**编译期**报错，std::uint16_t 这种字段一出现
   就编不过 —— 代价是丢掉 stream 兜底能打的其它类型，要先把框架里进日志的字段全检查一遍
   （现在已知 AppId / AppSettings 的 uint16 成员混在有原生 formatter 的字段里，得逐个确认）。
2. 给 efmt 补一个 unsigned short 的原生格式化器（submodule third_party/efmt-elog 走它自己的 GitHub Issues，
   本仓库的 issue tracker 不管它）。

## 备注

- 这条与 issue 13 是同一根源的两面：issue 13 为了绕开「1 字节整型被当字符打」把字段抬到 std::uint16_t，
  于是这些字段都落到了 stream 兜底上；宿主有几十 KB 栈看不出来，own task 只有几百字节就现形了。
- 唯一没测过的场景：SettingsApp / HelloApp 若也切 BackgroundPolicy::own_task，它们的日志预算需要按
  同一把尺子量一遍（当前 demo 里只有 TickerApp 用 own_task）。
