/**
 * Embark 宿主 · FreeRTOS 诊断桥的实现（issues/06）
 *
 * C 侧（FreeRTOSConfig.h 的 configASSERT、freertos_hooks.c）跨不了 C++ 边界，
 * 只能调到这三个 extern "C" 入口；这里是它们与 embark::assert_failed / fatal
 * 的转交层（std::cstring 的 strncpy 只是给任务名留现场，方便日志打出是哪个任务）。
 */
#include <cstring>

#include "FreeRTOS.h"  // configMAX_TASK_NAME_LEN（嵌入名单是 API 的一部分）

#include <embark/diagnostics.h>
#include <embark/log.h>

#include "embark_freertos_bridge.h"

namespace {

void log_freertos_fault(const char* what) {
  /* 走到这里说明内核判定这是编程错误；日志可能因此多走一层 sink，
   * 但 halt 之前把最后一条日志打出去正是 spec §9 的要求。 */
  ELOG_ERROR("FreeRTOS 诊断：{}", what);
}

}  // namespace

extern "C" {

void embark_freertos_assert_failed(const char* file, int line, const char* expression) {
  ELOG_ERROR("FreeRTOS 断言失败：{}（{}:{}）", expression, file, line);
  /* 与 embark::assert_failed 一致：最后一条日志 + halt（宿主实现见 host_system.cpp）。
   * 不走 ELOG_ERROR 的中间量，直接把现场交给 embark 的诊断入口，保持只有一条真相。 */
  ::embark::assert_failed(file, line, expression);
}

void embark_freertos_stack_overflow(const char* task_name) {
  char name[configMAX_TASK_NAME_LEN + 1U];  // configMAX_TASK_NAME_LEN 在 FreeRTOS.h 里
  name[configMAX_TASK_NAME_LEN] = '\0';
  std::strncpy(name, task_name != nullptr ? task_name : "?", configMAX_TASK_NAME_LEN);
  log_freertos_fault("任务栈溢出");
  ELOG_ERROR("爆栈任务：{}", name);
  ::embark::fatal("FreeRTOS 任务栈溢出");
}

void embark_freertos_malloc_failed(void) {
  log_freertos_fault("动态分配失败（纯静态配置下不该发生）");
  ::embark::fatal("FreeRTOS 堆耗尽");
}

}  // extern "C"