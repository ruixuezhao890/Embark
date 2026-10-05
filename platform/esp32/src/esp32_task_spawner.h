/**
 * Embark ESP32-S3 · 后台任务（own_task 策略）的创建与回收（issues/11、issue 15）
 *
 * 与宿主同构：这里只是"平台那一半"，状态机、容量核算与回收时机都在平台无关的
 * PooledTaskSpawner（platform/common/pooled_task_spawner.h）里。真机与宿主的差别只有三点：
 *   1) 落核：用 xTaskCreateStaticPinnedToCore()，后台任务固定落核 0（UI 落核 1），
 *      一边一个核，互不抢。
 *   2) **栈深口径**：框架的 stack_words 是"栈字"，而 ESP-IDF 的 xTaskCreate* 收的是**字节**
 *      —— IDF 头文件原文："@param ulStackDepth The size of the task stack specified as the
 *      NUMBER OF BYTES. Note that this differs from vanilla FreeRTOS."。框架那套字数（含
 *      config/embark_limits.h 的默认值）是按**宿主的字长**（x86-64 = 8 字节）调的，而真机的
 *      StackType_t 是 uint8_t —— 乘 sizeof(StackType_t) 等于乘 1，算出来的栈深只有宿主的
 *      1/8（issue 21 实测：UI 任务 2 KB 栈的栈指针直接掉到缓冲外面）。所以乘 esp32_board.h
 *      的 stack_word_bytes，让真机拿到与宿主相同的字节预算。
 *   3) 停放判据：SMP 内核里 eTaskGetState() 没有"自己 = eRunning"那条捷径
 *      （IDF tasks.c:2511-2518 把它包在 configNUMBER_OF_CORES == 1 里），所以"是不是
 *      还在某个核上跑"必须显式查 pxCurrentTCBs[own_task_core]。
 *
 * 为什么是"park + 持有者他删"（issue 15 的机制定案）：
 *   - 任务入口返回后不能就此消失：TCB 还挂在 FreeRTOS 的待清理链上（自删时由空闲任务
 *     收尾），静态存储一旦被下一个任务复用就是别名 —— 致命。所以入口返回后池的
 *     trampoline 会把它永久停下（suspend_self()），"入口已返回"由池记进槽状态。
 *   - 回收责任落在持有者（全工程 = 唯一 UI 任务）头上：release_task() 先确认 is_parked()
 *     再 delete_task()。删一个**不在任何核上跑**的任务，IDF 的 vTaskDelete() 走同步分支
 *     当场 prvDeleteTCB()（tasks.c:2252 判 xTaskIsRunningOrYielding，假则 :2324 直接回收）；
 *     纯静态分配下 prvDeleteTCB() 什么都不释放，TCB/栈数组可以安全复用。
 *   - 绝不从任务自己那一侧删（vTaskDelete(NULL)）：那是异步终止，TCB 要等空闲任务。
 *
 * is_parked() 为什么要两个条件：
 *   1) eTaskGetState() == eSuspended —— 已在悬挂链上；
 *   2) xTaskGetCurrentTaskHandleForCore(own_task_core) != handle —— 它不是本核的当前任务。
 *   只有 1) 是不够的：任务被插入悬挂链之后、真正从核上驱逐之前有一段窗口，此时
 *   xTaskRunState 仍是本核，vTaskDelete() 会走异步分支，把 TCB 挂进 xTasksWaitingTermination，
 *   而池会立刻复用那块静态内存 → 空闲任务随后对着被覆盖的链表项解引用。
 *   own task 钉在核 0，只可能被核 0 选中，所以查 own_task_core 一个核就够；悬挂任务
 *   没人 resume，两条都成立之后状态**永久稳定**。
 *
 * 容量：arena 在池里（.bss 的 alignas(16) 数组），槽类型 Esp32Task 在这里定义。
 * 一个槽 = 登记项 + StaticTask_t + 栈数组；栈按 esp32_board.h 的 own_task_stack_bytes 开
 * （= 框架的 own_task_stack_words 字在宿主机字长下的字节数：512 字 → 4 KB）。
 */
#ifndef EMBARK_PLATFORM_ESP32_TASK_SPAWNER_H
#define EMBARK_PLATFORM_ESP32_TASK_SPAWNER_H

#include <cstddef>
#include <cstdint>
#include <new>

#include <embark/task_spawner.h>
#include <embark_limits.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32_board.h"
#include "pooled_task_spawner.h"

namespace embark::platform::esp32 {

/// 一个静态槽：登记项 + 静态 TCB + 静态栈。池按 slot_bytes() 从 arena 里切出它。
struct Esp32Task final {
  /// 登记项必须是第一个成员（池用 Kernel::header_of() 取回）。
  PoolTask header{};
  /// 静态 TCB。xTaskCreateStatic*() 会把它整个 memset 成 0 再初始化（IDF tasks.c:1300）。
  StaticTask_t tcb{};
  /// 静态栈。IDF 收字节数，这里按字数组声明（口径见文件头第 2 点）。
  alignas(16) StackType_t stack[own_task_stack_bytes]{};
};

/// 真机的任务内核：ESP-IDF（FreeRTOS SMP）上的"建 / 查停放 / 删 / 自停"。
/// 全 static、无状态 —— 池按类型调用，不持有它的实例。
struct Esp32TaskKernel final {
  using Task = Esp32Task;
  using KernelEntry = void (*)(void*);

  /// 槽占用的字节数（StaticPool 的 16 字节粒度向上取整）。
  static constexpr std::size_t slot_bytes() noexcept {
    return (sizeof(Task) + (StaticPool::granule_bytes - 1U)) & ~(StaticPool::granule_bytes - 1U);
  }

  static PoolTask& header_of(Task& task) noexcept { return task.header; }

  /// 在池给的槽上建任务。先登记再建：任务落在另一个核（核 0），建完就可能开跑，
  /// 登记项晚一步写就会被读到垃圾。失败返回 nullptr。
  static Task* make_task(std::uint8_t* storage, const char* name, KernelEntry trampoline,
                         const PoolTask& record, std::uint16_t stack_words,
                         std::uint16_t priority) noexcept {
    if (storage == nullptr || name == nullptr || trampoline == nullptr || stack_words == 0U) {
      return nullptr;
    }
    if (stack_words > embark::own_task_stack_words) {
      return nullptr;  // 池已经挡过一次，这里只是不让内核自己越界
    }

    auto* const task = new (storage) Task();
    task->header = record;

    // IDF 的 ulStackDepth 是字节数：框架的字数按宿主字长折算成真机字节数（见 esp32_board.h）。
    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
        trampoline, name,
        static_cast<std::uint32_t>(stack_words) * static_cast<std::uint32_t>(stack_word_bytes), task,
        priority, task->stack, &task->tcb, embark::platform::esp32::own_task_core);
    if (handle == nullptr) {
      task->~Task();  // 静态创建理论不失败；真失败就把槽还原成未构造的样子
      return nullptr;
    }
    return task;
  }

  /// 真的停下了吗？两个条件都成立才算（理由见文件头）。
  static bool is_parked(const Task& task) noexcept {
    const TaskHandle_t handle = handle_of(task);
    if (xTaskGetCurrentTaskHandleForCore(embark::platform::esp32::own_task_core) == handle) {
      return false;  // 还是核 0 的当前任务：绝不算停放
    }
    return eTaskGetState(handle) == eSuspended;
  }

  /// 删掉一个已 park 的任务（同步回收，见文件头）。只他删，绝不自删。
  static void delete_task(Task& task) noexcept {
    const TaskHandle_t handle = handle_of(task);
    if (xTaskGetCurrentTaskHandleForCore(embark::platform::esp32::own_task_core) == handle) {
      return;  // 防御：从任务自己这一侧删会走异步终止，TCB 要等空闲任务
    }
    vTaskDelete(handle);
  }

  /// 把当前任务永久停下（池的 trampoline 在入口返回后调它）。
  static void suspend_self() noexcept {
    vTaskSuspend(nullptr);
    for (;;) {
      // vTaskSuspend(nullptr) 不该返回；万一返回了也绝不能让任务入口跑出去。
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

 private:
  /// StaticTask_t 与 TaskHandle_t 是内核的两个不同类型（IDF task.h:96 是
  /// `struct tskTaskControlBlock *`），xTaskCreateStatic* 内部也是这么转的。
  static TaskHandle_t handle_of(const Task& task) noexcept {
    return reinterpret_cast<TaskHandle_t>(const_cast<StaticTask_t*>(&task.tcb));
  }
};

/// 后台任务池的真机形态：整固件一个实例（main.cpp 的 static 对象），交给 Framework。
using Esp32TaskSpawner = PooledTaskSpawner<Esp32TaskKernel>;

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_TASK_SPAWNER_H */
