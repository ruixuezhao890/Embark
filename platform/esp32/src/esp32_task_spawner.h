/**
 * Embark ESP32-S3 · 后台任务（own_task 策略）的创建器（issues/11）
 *
 * 与宿主同构：静态 TCB + 静态栈（BSS），零堆。容量来自 config/embark_limits.h
 * 的 max_own_tasks / own_task_stack_words；槽位编译期定死，超出返回 Error::busy。
 *
 * 两个平台差异（都在这里一次性抹平）：
 *   1) 落核：IDF 有 xTaskCreateStaticPinnedToCore，后台任务默认落核 0
 *      （UI 任务落核 1 —— 一边一个核，互不抢）。
 *   2) **栈深口径**：框架的 `stack_words` 是 StackType_t 字（与宿主一致），而
 *      ESP-IDF 的 xTaskCreate* 收的是**字节** —— IDF 头文件原文：
 *      "@param ulStackDepth The size of the task stack specified as the NUMBER OF
 *       BYTES. Note that this differs from vanilla FreeRTOS."
 *      所以这里乘一次 sizeof(StackType_t)，保证"512 字 = 2 KB"在两边是同一个意思。
 */
#ifndef EMBARK_PLATFORM_ESP32_TASK_SPAWNER_H
#define EMBARK_PLATFORM_ESP32_TASK_SPAWNER_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <embark/task_spawner.h>
#include <embark_limits.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32_board.h"

namespace embark::platform::esp32 {

/// 后台任务创建器（xTaskCreateStaticPinnedToCore）。整固件一个实例，交给 Framework。
class Esp32TaskSpawner final : public ITaskSpawner {
 public:
  Esp32TaskSpawner() noexcept = default;

  [[nodiscard]] Error spawn_task(const char* name, void (*entry)(void*) noexcept, void* argument,
                                 std::uint16_t stack_words,
                                 std::uint8_t priority) noexcept override {
    if (used_ >= embark::max_own_tasks) {
      return Error::busy;  // 静态槽位（TCB/栈数组）耗尽
    }
    if (name == nullptr || entry == nullptr || stack_words == 0U) {
      return Error::invalid_argument;
    }
    if (stack_words > embark::own_task_stack_words) {
      return Error::no_space;  // 槽位的栈容量不够 App 要的
    }

    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
        entry, name, static_cast<std::uint32_t>(stack_words) * sizeof(StackType_t), argument,
        priority, stacks_[used_], &tcbs_[used_], own_task_core);
    if (handle == nullptr) {
      return Error::no_space;  // 静态创建理论不失败，防御
    }
    ++used_;
    return Error::none;
  }

  /// 已经起来了几个后台任务（启动日志用）。
  [[nodiscard]] std::size_t spawned() const noexcept { return used_; }

 private:
  StaticTask_t tcbs_[embark::max_own_tasks];
  StackType_t stacks_[embark::max_own_tasks][embark::own_task_stack_words];
  std::size_t used_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_TASK_SPAWNER_H */
