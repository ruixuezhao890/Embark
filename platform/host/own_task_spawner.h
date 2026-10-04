/**
 * Embark 宿主 · 后台任务（own_task 策略）的创建器（issues/07）
 *
 * ITaskSpawner 的宿主实现：与 UI 任务同构 —— 静态 TCB + 静态栈（BSS），零堆
 * （FreeRTOSConfig.h：configSUPPORT_DYNAMIC_ALLOCATION 0）。
 *
 * 容量来自 config/embark_limits.h：max_own_tasks 个任务，每个栈
 * own_task_stack_words 字（宿主 512 字 = 2 KB）。槽位是编译期定死的，
 * 超出返回 Error::busy。
 */
#ifndef EMBARK_PLATFORM_HOST_OWN_TASK_SPAWNER_H
#define EMBARK_PLATFORM_HOST_OWN_TASK_SPAWNER_H

#include <cstddef>
#include <cstdint>

#include "FreeRTOS.h"
#include "task.h"

#include <embark/error.h>
#include <embark/task_spawner.h>
#include <embark_limits.h>

namespace embark::platform::host {

/// 后台任务创建器（xTaskCreateStatic）。用法：整进程一个实例，交给 Framework。
class HostTaskSpawner final : public ITaskSpawner {
 public:
  HostTaskSpawner() noexcept = default;

  [[nodiscard]] Error spawn_task(const char* name, void (*entry)(void*) noexcept,
                                 void* argument, std::uint16_t stack_words,
                                 std::uint8_t priority) noexcept override {
    if (used_ >= embark::max_own_tasks) {
      return Error::busy;  // 静态槽位（TCB/栈数组）耗尽
    }
    if (name == nullptr || entry == nullptr || stack_words == 0) {
      return Error::invalid_argument;
    }
    if (stack_words > embark::own_task_stack_words) {
      return Error::no_space;  // 槽位的栈容量不够 App 要的
    }

    TaskHandle_t handle =
        xTaskCreateStatic(entry, name, stack_words, argument, priority,
                          stacks_[used_], &tcbs_[used_]);
    if (handle == nullptr) {
      return Error::no_space;  // 静态创建理论不失败，防御
    }
    ++used_;
    return Error::none;
  }

 private:
  StaticTask_t tcbs_[embark::max_own_tasks];
  StackType_t stacks_[embark::max_own_tasks][embark::own_task_stack_words];
  std::size_t used_ = 0;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_OWN_TASK_SPAWNER_H */