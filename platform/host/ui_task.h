/**
 * Embark 宿主 · 唯一 UI 任务（issues/06）
 *
 * spec §6 在宿主上的落地：全局唯一的一个 FreeRTOS 任务承载 UI 循环。
 * 三条铁律（与真机同构，见 issue 11）：
 *   - 用 xTaskCreateStatic：任务控制块与栈都是 BSS 里的静态存储，
 *     FreeRTOSConfig.h 里 configSUPPORT_DYNAMIC_ALLOCATION 0，内核没有堆；
 *   - 任务函数不许返回：收尾走 exit_process()（_Exit），因为主线程还卡在
 *     vTaskStartScheduler 的模拟中断循环里（issue 02 实测：vTaskEndScheduler
 *     会挂死，宿主没有"停调度器再回 main"这条路）；
 *   - 优先级/栈深在 config/embark_limits.h 定死，真机另配。
 *
 * 用法（ui_demo.cpp）：
 *   start_ui_task(&ui_main, nullptr);   // 建任务（还没起调度器）
 *   start_scheduler();                  // 永不返回；调度器接管后 UI 任务才真正开始跑
 */
#ifndef EMBARK_PLATFORM_HOST_UI_TASK_H
#define EMBARK_PLATFORM_HOST_UI_TASK_H

#include <cstdint>

#include "FreeRTOS.h"
#include "task.h"

#include <embark/error.h>
#include <embark_limits.h>

namespace embark::platform::host {

/// UI 任务入口：参数是 start_ui_task 传的 argument。不许返回（见文件头）。
using UiTaskEntry = void (*)(void* argument) noexcept;

/// 任务参数（默认值 = config/embark_limits.h 的宿主值）。
struct UiTaskConfig {
  const char* name = "embark-ui";  ///< ≤ configMAX_TASK_NAME_LEN-1（FreeRTOS 保留结尾 \0）
  std::uint16_t stack_words = static_cast<std::uint16_t>(embark::ui_task_stack_words);
  std::uint8_t priority = embark::ui_task_priority;
};

/// 创建唯一的 UI 任务（静态存储，零堆）。返回 busy = 已经建过；
/// invalid_argument = entry 为空或栈深不在 (0, ui_task_stack_words]。
[[nodiscard]] Error start_ui_task(UiTaskEntry entry, void* argument,
                                  const UiTaskConfig& config = UiTaskConfig{}) noexcept;

/// 起调度器。按 FreeRTOS 语义 + 上游 MSVC-MingW 端口，永不返回（issue 02）。
void start_scheduler() noexcept;

/// UI 循环的一拍间隔：vTaskDelay 让出（绝不忙等）。period_ms 默认 5（spec §6）。
void ui_loop_delay(std::uint32_t period_ms = embark::ui_loop_period_ms) noexcept;

/// 宿主进程收尾：_Exit(code)。任务函数最后必须调它（见文件头）。
[[noreturn]] void exit_process(int code) noexcept;

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_UI_TASK_H */