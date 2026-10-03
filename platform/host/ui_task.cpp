/**
 * Embark 宿主 · 唯一 UI 任务的实现（issues/06）
 *
 * 见 ui_task.h 的文件头注释。这里是"静态存储 + 永不返回"的落地：
 * 任务控制块和栈是文件级 static（BSS，零初始化），入口挂在一个小跳板里
 * （FreeRTOS 只给任务传一个 void*，我们把 {入口, 参数} 打包传进去）。
 */
#include <cstdlib>

#include <embark/diagnostics.h>

#include "ui_task.h"

namespace {

// --- 编译期静态断言：这份代码只允许跑在"纯静态内核"上（spec §10）-------------
static_assert(configSUPPORT_STATIC_ALLOCATION == 1,
              "宿主内核必须启用静态分配（FreeRTOSConfig.h）");
static_assert(configSUPPORT_DYNAMIC_ALLOCATION == 0,
              "宿主内核必须是零堆（configSUPPORT_DYNAMIC_ALLOCATION 0），"
              "这样任何动态分配 API 在编译期就不存在");

// 静态存储：任务控制块 + 任务栈（栈深来自 config/embark_limits.h，编译期定死）。
StaticTask_t ui_task_tcb;
StackType_t ui_task_stack[embark::ui_task_stack_words];

bool task_started = false;
TaskHandle_t ui_task_handle = nullptr;

// FreeRTOS 的 xTaskCreateStatic 只传一个 void* 给任务函数：把入口和参数打包。
struct UiTaskLaunch {
  embark::platform::host::UiTaskEntry entry;
  void* argument;
};
UiTaskLaunch launch = {nullptr, nullptr};

void ui_task_thunk(void* argument) noexcept {
  const UiTaskLaunch& task_launch = *static_cast<const UiTaskLaunch*>(argument);
  task_launch.entry(task_launch.argument);
  // 任务函数不许返回（见 ui_task.h）；真返回了说明调用方忘了 exit_process。
  ::embark::fatal("UI 任务入口返回了：宿主收尾必须走 exit_process()");
}

}  // namespace

namespace embark::platform::host {

Error start_ui_task(UiTaskEntry entry, void* argument,
                    const UiTaskConfig& config) noexcept {
  if (entry == nullptr) {
    return Error::invalid_argument;
  }
  if (config.stack_words == 0U ||
      config.stack_words > static_cast<std::uint16_t>(embark::ui_task_stack_words)) {
    return Error::invalid_argument;
  }
  if (task_started) {
    return Error::busy;  // 全工程只有一个 UI 任务（spec §6）
  }

  launch.entry = entry;
  launch.argument = argument;

  ui_task_handle = xTaskCreateStatic(ui_task_thunk, config.name, config.stack_words,
                                     &launch, config.priority,
                                     ui_task_stack, &ui_task_tcb);
  if (ui_task_handle == nullptr) {
    return Error::no_space;  // 理论上到不了：静态分配不会失败，防御而已
  }
  task_started = true;
  return Error::none;
}

void start_scheduler() noexcept {
  vTaskStartScheduler();
  // V10.6.2 的 MSVC-MingW 端口里 xPortStartScheduler 的模拟中断循环永不返回
  // （issue 02 实测）；真返回了说明配置或端口出问题了。
  ::embark::fatal("vTaskStartScheduler 返回了（不该发生）");
}

void ui_loop_delay(std::uint32_t period_ms) noexcept {
  vTaskDelay(pdMS_TO_TICKS(period_ms));
}

void exit_process(int code) noexcept {
  // 为什么是 _Exit 而不是 exit/return：
  //   主线程还卡在 vTaskStartScheduler 的模拟中断循环里，exit() 的 C 运行时清理
  //   会 join 所有线程并试图正常退出 —— 那条循环等不到，于是进程挂死（issue 02
  //   实测 vTaskEndScheduler 挂死，vPortEndScheduler 只清一个标志位）。
  //   _Exit 不跑析构/atexit，直接给退出码 —— 宿主模拟器要的只是"干净的退出码"。
  std::_Exit(code);
}

}  // namespace embark::platform::host