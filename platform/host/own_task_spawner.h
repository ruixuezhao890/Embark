/**
 * Embark 宿主 · 后台任务（own_task 策略）的创建与回收（issues/11、issue 15）
 *
 * 这里是"平台那一半"：把 FreeRTOS Windows port 的建任务 / 查停放 / 删任务收成一个
 * Kernel，交给平台无关的 PooledTaskSpawner（platform/common/pooled_task_spawner.h）驱动。
 * 状态机、容量核算与回收时机都在池那边，本文件只回答"怎么在内核里做这四件事"。
 *
 * 为什么是"park + 持有者他删"（issue 15 的机制定案）：
 *   - 任务入口返回后不能就此消失：TCB 还挂在 FreeRTOS 的待清理链上，静态存储一旦被下一个
 *     任务复用就是别名。所以入口返回后池的 trampoline 会把它永久停下（suspend_self()），
 *     "入口已返回"这件事由池记进槽状态。
 *   - 回收责任落在持有者（全工程 = 唯一 UI 任务）头上：release_task() 先确认 is_parked()
 *     再 delete_task()。宿主上删一个**已悬挂**的任务是同步的 —— vTaskDelete() 在
 *     pxTCB != pxCurrentTCB 时直接 prvDeleteTCB()（third_party/freertos/tasks.c:1192-1195），
 *     TCB 当场摘干净。
 *   - 绝不从任务自己那一侧删（vTaskDelete(NULL)）：那会走 ExitThread(0) 并留下待清理 TCB。
 *
 * is_parked() 为什么要两个条件：
 *   1) eTaskGetState() == eSuspended —— 已在悬挂链上；
 *   2) 该任务不是当前任务 —— 宿主的 eTaskGetState() 本来就对"自己"直接返回 eRunning
 *      （tasks.c:1361），所以这一条在宿主上是把同一件事写明；真机（SMP）没有那个捷径，
 *      必须额外查 pxCurrentTCBs[core]（见 platform/esp32/src/esp32_task_spawner.h）。
 *   两条都成立之后状态就**永久稳定**：悬挂链上的任务没人 resume，永远不会再被选中。
 *   池据此才敢立刻复用槽内存。
 *
 * 容量：arena 在池里（.bss 的 alignas(16) 数组），槽类型 HostTask 在这里定义。
 * 一个槽 = 登记项 + StaticTask_t + 栈数组；宿主 512 字 = 4 KB（StackType_t = size_t）。
 * 宿主端口并不真的拿我们的栈数组当栈用：pxPortInitialiseStack() 只在栈顶下方放一个
 * ThreadState_t（port.c:226-256），真正的线程栈由 CreateThread 自己分配 —— 但栈数组仍
 * 必须非空，stack_words == 0 会让它写到 NULL，所以池把 0 挡成 invalid_argument。
 */
#ifndef EMBARK_PLATFORM_HOST_OWN_TASK_SPAWNER_H
#define EMBARK_PLATFORM_HOST_OWN_TASK_SPAWNER_H

#include <cstddef>
#include <cstdint>
#include <new>

#include <embark/task_spawner.h>
#include <embark_limits.h>

#include "FreeRTOS.h"
#include "task.h"

#include "pooled_task_spawner.h"

namespace embark::platform::host {

/// 一个静态槽：登记项 + 静态 TCB + 静态栈。池按 slot_bytes() 从 arena 里切出它。
struct HostTask final {
  /// 登记项必须是第一个成员（池用 Kernel::header_of() 取回）。
  PoolTask header{};
  /// 静态 TCB。xTaskCreateStatic() 会把它整个 memset 成 0 再初始化（tasks.c:598）。
  StaticTask_t tcb{};
  /// 静态栈。宿主端口只在这里放 ThreadState_t，但长度口径仍是"字 × sizeof(StackType_t)"。
  alignas(16) StackType_t stack[embark::own_task_stack_words]{};
};

/// 宿主的任务内核：FreeRTOS Windows port 上的"建 / 查停放 / 删 / 自停"。
/// 全 static、无状态 —— 池按类型调用，不持有它的实例。
struct HostTaskKernel final {
  using Task = HostTask;
  using KernelEntry = void (*)(void*);

  /// 槽占用的字节数（StaticPool 的 16 字节粒度向上取整）。
  static constexpr std::size_t slot_bytes() noexcept {
    return (sizeof(Task) + (StaticPool::granule_bytes - 1U)) & ~(StaticPool::granule_bytes - 1U);
  }

  static PoolTask& header_of(Task& task) noexcept { return task.header; }

  /// 在池给的槽上建任务。先登记再建：xTaskCreateStatic() 有可能当场把新任务切上来跑
  /// （优先级更高时），登记项晚一步写就会被读到垃圾。失败返回 nullptr。
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

    TaskHandle_t handle =
        xTaskCreateStatic(trampoline, name, static_cast<std::uint32_t>(stack_words), task, priority,
                          task->stack, &task->tcb);
    if (handle == nullptr) {
      task->~Task();  // 静态创建理论不失败；真失败就把槽还原成未构造的样子
      return nullptr;
    }
    return task;
  }

  /// 真的停下了吗？两个条件都成立才算（理由见文件头）。
  static bool is_parked(const Task& task) noexcept {
    const TaskHandle_t handle = handle_of(task);
    if (xTaskGetCurrentTaskHandle() == handle) {
      return false;  // 还在被调度（含"就是当前任务"）：绝不算停放
    }
    return eTaskGetState(handle) == eSuspended;
  }

  /// 删掉一个已 park 的任务（同步回收，见文件头）。只他删，绝不自删。
  static void delete_task(Task& task) noexcept {
    const TaskHandle_t handle = handle_of(task);
    if (xTaskGetCurrentTaskHandle() == handle) {
      return;  // 防御：从任务自己这一侧删会留下待清理 TCB
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
  /// StaticTask_t 与 TaskHandle_t 是内核的两个不同类型（宿主 task.h:92 是
  /// `struct tskTaskControlBlock *`），xTaskCreateStatic 内部也是这么转的。
  static TaskHandle_t handle_of(const Task& task) noexcept {
    return reinterpret_cast<TaskHandle_t>(const_cast<StaticTask_t*>(&task.tcb));
  }
};

/// 后台任务池的宿主形态：Framework 与两个宿主可执行文件各声明一个 static 对象
/// （.bss，不占 UI 任务栈）。
using HostTaskSpawner = PooledTaskSpawner<HostTaskKernel>;

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_OWN_TASK_SPAWNER_H */
