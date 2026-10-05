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

/**
 * LVGL 内存观测：当前未回收字节、历史峰值、预算上限。
 *
 * 这两个数字是"零堆"验收的证据来源（真机没有宿主那套全局 new/delete 钩子，只能看
 * 分配器自己还剩多少）。平台共用层的 UI 端口在收尾时打一行，宿主与真机各自实现：
 *   - 宿主：platform/host/host_lvgl_mem.cpp（malloc 包装的计数器）
 *   - 真机：platform/esp32/src/esp32_lvgl_mem.cpp（定容静态池）
 */
size_t embark_lvgl_outstanding_bytes(void);
size_t embark_lvgl_peak_bytes(void);
size_t embark_lvgl_budget_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBARK_LVGL_HOOKS_H */