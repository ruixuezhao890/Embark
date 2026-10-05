# 21 · 真机任务栈口径：xtensa 的 StackType_t 是 uint8_t，乘 sizeof() 等于乘 1

Status: resolved
Type: task
Blocked by: 11
来源: 真机 bring-up（首次烧录即 panic：Core 1 LoadProhibited，UI 任务栈实际只有 2 KB）

## 现象（证据）

第一次烧进去的镜像（HEAD 8abc70a 的栈口径）在串口上逐字节稳定复现同一个 panic，
16 个启动周期、18 次 panic，全部一字不差（evidence/21-stack-unit/pre-fix-serial-excerpt.txt）：

    I (760) app_init: App version:      8abc70a-dirty
    [main.cpp:106 app_main] Embark 0.1.0 启动（平台 esp32）：… NVS 容量 2048 字节；UI 任务栈 2048 字，周期 5 ms
    [demo_apps.cpp:233 onCreate] App ticker onCreate（own_task，周期 50 ms，栈 256 字，优先级 4）
    Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
    EXCVADDR: 0xa5a5a5b1  LBEG    : 0x40056f08  LEND    : 0x40056f12  LCOUNT  : 0x00000000
    Backtrace: 0x4205ecad:0x3fc9b0a0 0x4205f6fd:0x3fc9b0c0 0x4205f853:0x3fc9b120 0x4200e825:0x3fc9b160
               0x4200d5ed:0x3fc9b1b0 0x4201260d:0x3fc9b800 0x4037da59:0x3fc9b820

读法：UI 任务栈 2048 字 是框架**想**说的 2048 个字，真机拿到的却是 2048 **字节**；panic 在 Core 1
（UI 任务所在核；own task 一律钉在 core 0），EXCVADDR 0xa5a5a5b1 是指针变量被 FreeRTOS 的栈填充字节
0xa5 覆盖后的读数 —— 栈指针掉到栈缓冲**外面**，先踩掉紧邻的 App 注册表与 efmt 的格式化上下文，
再在别处（这里是 xTaskIncrementTick / prvSelectHighestPriorityTaskSMP 那条链）炸出来。

## 原因

- config/embark_limits.h 的 ui_task_stack_words = 2048 / own_task_stack_words = 512 是按**宿主的字长**
  定的（x86-64 上 StackType_t = size_t = 8 字节 ⇒ 16 KB / 4 KB）。
- 真机的 StackType_t 是 uint8_t：
  E:/00_Software/03_Dev_Env/IDF/components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h:88-91
      #define portSTACK_TYPE              uint8_t
      typedef portSTACK_TYPE              StackType_t;
  于是老的换算 stack_words * sizeof(StackType_t) 等于 stack_words * 1：UI 任务只剩 2048 B、
  own task 只剩 512 B（TickerApp 自己覆盖成 256 B）。文件里的字面量没错，**单位**错了。
- 注：IDF 的 xTaskCreate* 的 ulStackDepth 本来就是**字节**（与 vanilla FreeRTOS 的"字"不同），
  uxTaskGetStackHighWaterMark() 在 xtensa 上返回的也是字节（1 word = 1 byte），只是数值上刚好等于字节。

## 修复

- platform/esp32/src/esp32_board.h:118-126：新增唯一换算点
      inline constexpr std::size_t stack_word_bytes = 8U;
      inline constexpr std::size_t ui_task_stack_bytes = embark::ui_task_stack_words * stack_word_bytes;
      inline constexpr std::size_t own_task_stack_bytes = embark::own_task_stack_words * stack_word_bytes;
  上面的长注释解释了为什么按宿主的字长折、以及要省 SRAM 就只动这一行。
- platform/esp32/src/esp32_ui_task.h：UiTaskConfig.stack_words → stack_bytes（语义改成字节，默认值取
  ui_task_stack_bytes）；esp32_ui_task.cpp：StackType_t ui_task_stack[ui_task_stack_bytes]、
  校验 config.stack_bytes > ui_task_stack_bytes、创建时把 config.stack_bytes 原样交给 IDF。
- platform/esp32/src/esp32_task_spawner.h：alignas(16) StackType_t stack[own_task_stack_bytes]{};
  创建时传 stack_words * stack_word_bytes（原来乘 sizeof(StackType_t)，在 xtensa 上等于乘 1）。
- platform/esp32/project/main/main.cpp:106-108：启动日志的 UI 任务栈 {} 字 改成 {} 字节 + ep::ui_task_stack_bytes
  —— 让日志说真机拿到多少**字节**，而不是框架想要多少字（这条日志就是本 issue 的第一现场）。
- docs/common-pitfalls.md：栈口径两条改写（框架的 *_stack_words 是宿主字长；唯一换算点是 stack_word_bytes；
  IDF 收字节；别再在别处乘一次 sizeof(StackType_t)）。

## 验收

- 符号表对得上：xtensa-esp32s3-elf-nm -S build-esp32/embark_esp32.elf | findstr ui_task_stack →
      3fc9cd84 00004000 b _ZN6embark8platform5esp3212_GLOBAL__N_113ui_task_stackE   （0x4000 = 16384 字节）
- 启动日志变成 UI 任务栈 16384 字节，周期 5 ms，且 Core 1 panic 消失
  （evidence/22-own-task-stack/post-fix-clean-run.txt）。
- 心跳自证余量：心跳：2000 帧；… UI 栈余 11612 字节（16384 - 11612 = 4772 B 峰值用量）。
- 宿主单测不受影响：tests/kernel/test_own_task_lifecycle.cpp:255 的
  CHECK(spawner.stack_words_of(0U) == 256U)、:330 的
  CHECK(spawner.stack_words_of(default_stack_id) == embark::own_task_stack_words) 断言的是"字"口径，
  与后端换算无关。

## 备注

- 修完这条设备**还是没有跑起来**：Core 1 的 panic 没有了，回到 Core 0 继续炸 —— 那是独立的下一条
  issue 22（own task 里 efmt 的 std::ostringstream 兜底要 ~1.9 KB 栈）。两条要一起修才看到干净启动。
- 现在仍以"宿主的字长"为基准（真机拿到与宿主相同的字节预算）。真机要省 SRAM 就只改 stack_word_bytes
  一行，然后看心跳里的 UI 栈余量再收。
