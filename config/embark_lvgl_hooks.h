/**
 * @file embark_lvgl_hooks.h
 * @brief LVGL 需要的两个 C 侧钩子：内存分配器与断言处理（被 LVGL 的 C 源码包含）。
 *
 * 为什么单独一个头：`config/lv_conf.h` 里用
 *   #define LV_MEM_CUSTOM_INCLUDE "embark_lvgl_hooks.h"
 *   #define LV_ASSERT_HANDLER_INCLUDE "embark_lvgl_hooks.h"
 * 把这两个钩子指到这里。LVGL 是 C 代码，所以本文件必须是纯 C 头（extern "C" 包住），
 * 且不能引入任何 C++ 头。
 *
 * 实现按平台给：宿主在 platform/host/host_lvgl_mem.cpp（malloc 包装 + 计数 + 预算检查），
 * 目标（issue 11）再定内部 SRAM / PSRAM 策略。LVGL 只会在 UI 线程/任务里被触碰，
 * 所以这两个钩子不需要自己加锁（spec §10）。
 */
#ifndef EMBARK_LVGL_HOOKS_H
#define EMBARK_LVGL_HOOKS_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LVGL 的堆。参数与返回值语义同 malloc/free/realloc。 */
void* embark_lvgl_alloc(size_t size);
void embark_lvgl_free(void* pointer);
void* embark_lvgl_realloc(void* pointer, size_t size);

/** LVGL 断言失败（LV_ASSERT / LV_ASSERT_MSG）：记录后不返回。 */
void embark_lvgl_assert_failed(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBARK_LVGL_HOOKS_H */
