/**
 * Embark ESP32-S3 · 唯一 UI 任务（issues/11）
 *
 * spec §6：全工程只有一个 UI 任务，它跑框架循环、也是唯一碰 LVGL 的任务。
 * 这里用静态 TCB + 静态栈（零堆），并且**落核 1**（后台任务落核 0，一边一个）。
 *
 * 与宿主的区别：真机没有"退出进程"这条路 —— 所以这里不提供 exit_process()，
 * 也不提供 start_scheduler()（IDF 的调度器开机就在跑了）。
 */
#ifndef EMBARK_PLATFORM_ESP32_UI_TASK_H
#define EMBARK_PLATFORM_ESP32_UI_TASK_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <embark_limits.h>

#include "esp32_board.h"

namespace embark::platform::esp32 {

/// UI 任务入口：跑起来就不返回（返回 = UI 任务死了，按 fatal 处理）。
using UiTaskEntry = void (*)(void* argument) noexcept;

/// 任务参数（默认值 = esp32_board.h 的真机字节预算 + 落核 1）。
/// 栈深口径是**字节**：真机的 StackType_t 是 uint8_t，IDF 的 ulStackDepth 本来就是字节
/// （见 esp32_task_spawner.h 文件头第 2 点）—— 照字面搬宿主的字数只剩 1/8（issue 21）。
struct UiTaskConfig {
  const char* name = "embark-ui";
  std::uint16_t stack_bytes = static_cast<std::uint16_t>(ui_task_stack_bytes);
  std::uint8_t priority = embark::ui_task_priority;
  std::uint8_t core = ui_task_core;
};

/**
 * 创建唯一 UI 任务（静态存储，零堆）。
 *
 * 返回：none = 起来了；busy = 已经建过（唯一 UI 任务已经有主）；
 *       invalid_argument = entry 空 / 名字空 / 栈深为 0 或超过 ui_task_stack_bytes；
 *       no_space = 内核拒绝（理论不发生，防御）。
 */
[[nodiscard]] Error start_ui_task(UiTaskEntry entry, void* argument,
                                  const UiTaskConfig& config = UiTaskConfig{}) noexcept;

/// UI 循环的一跳：让出 CPU。与 Esp32Time::delay_ms 同一条纪律 —— ticks 不能为 0。
void ui_loop_delay(std::uint32_t period_ms = embark::ui_loop_period_ms) noexcept;

/// UI 任务栈的历史最低剩余（**字节**，uxTaskGetStackHighWaterMark 返回的是字，这里换算过）。
[[nodiscard]] std::size_t ui_task_stack_high_water_bytes() noexcept;

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_UI_TASK_H */
