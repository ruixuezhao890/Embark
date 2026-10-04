/**
 * Embark ESP32-S3 · 唯一 UI 任务实现（issues/11）
 */
#include "esp32_ui_task.h"

#include <embark/diagnostics.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace embark::platform::esp32 {
namespace {

// 整固件只有一个 UI 任务：槽位就是一个（BSS，零堆）。
StaticTask_t ui_task_tcb = {};
StackType_t ui_task_stack[embark::ui_task_stack_words] = {};
TaskHandle_t ui_task_handle = nullptr;

// 入口与参数在创建之前存好，蹦床函数再去取（只有一个任务，不需要每槽上下文）。
UiTaskEntry ui_entry = nullptr;
void* ui_argument = nullptr;

/// 蹦床：IDF 的任务入口签名是 void(*)(void*)，这里转到框架的 UI 循环。
void ui_task_trampoline(void* argument) noexcept {
  (void)argument;
  ui_entry(ui_argument);
  // 走到这里说明 UI 循环返回了 —— 唯一 UI 任务退出 = 整个框架停摆，不能装作没事。
  embark::fatal("UI 任务入口返回了：唯一 UI 任务不该退出");
}

}  // namespace

Error start_ui_task(UiTaskEntry entry, void* argument, const UiTaskConfig& config) noexcept {
  if (ui_task_handle != nullptr) {
    return Error::busy;  // 唯一 UI 任务已经有主
  }
  if (entry == nullptr || config.name == nullptr) {
    return Error::invalid_argument;
  }
  if (config.stack_words == 0U || config.stack_words > embark::ui_task_stack_words) {
    return Error::invalid_argument;  // 静态槽位就这么大，不能要更多
  }

  ui_entry = entry;
  ui_argument = argument;

  // ESP-IDF 的栈深单位是**字节**（与 vanilla FreeRTOS 的字不同，见 esp32_task_spawner.h 的说明）。
  ui_task_handle = xTaskCreateStaticPinnedToCore(
      &ui_task_trampoline, config.name,
      static_cast<std::uint32_t>(config.stack_words) * sizeof(StackType_t), nullptr,
      config.priority, ui_task_stack, &ui_task_tcb, config.core);
  if (ui_task_handle == nullptr) {
    ui_entry = nullptr;
    ui_argument = nullptr;
    return Error::no_space;  // 静态创建理论不失败，防御
  }
  return Error::none;
}

void ui_loop_delay(std::uint32_t period_ms) noexcept {
  TickType_t ticks = pdMS_TO_TICKS(period_ms);
  if (ticks == 0 && period_ms > 0U) {
    ticks = 1;  // 100 Hz 下 pdMS_TO_TICKS(5) == 0：让出 0 拍等于忙等
  }
  vTaskDelay(ticks);
}

std::size_t ui_task_stack_high_water_bytes() noexcept {
  if (ui_task_handle == nullptr) {
    return 0U;
  }
  // uxTaskGetStackHighWaterMark 返回的是**字**（IDF 头文件原文："in words"，与 create 的字节口径相反）。
  return static_cast<std::size_t>(uxTaskGetStackHighWaterMark(ui_task_handle)) *
         sizeof(StackType_t);
}

}  // namespace embark::platform::esp32
