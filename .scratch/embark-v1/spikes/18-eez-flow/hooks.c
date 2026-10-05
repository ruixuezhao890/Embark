/**
 * @file hooks.c
 * @brief Spike 18 的 LVGL 内存钩子：纯计数版（不设预算上限）。
 *
 * 目的：先测出 EEZ Flow 运行时的真实 LVGL 堆增量/峰值，再与仓库 256 KB 预算
 * （config/embark_limits.h 的 lvgl_alloc_budget_bytes）对比。
 * 语义对齐 platform/host/host_lvgl_mem.cpp（BlockHeader + 计数器），
 * 但不编译仓库那份（它依赖 elog + embark::fatal）。
 */
#include "embark_lvgl_hooks.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef union {
    size_t size;
    long double _align_ld;
    void *_align_p;
} Header;

#define HEADER_SIZE sizeof(Header)

static size_t g_outstanding = 0;
static size_t g_peak = 0;
static size_t g_budget = 256u * 1024u; /* 只做对比基准，不拦截 */

void *embark_lvgl_alloc(size_t size) {
    if (size == 0) size = 1;
    Header *h = (Header *)malloc(HEADER_SIZE + size);
    if (!h) return NULL;
    h->size = size;
    g_outstanding += size;
    if (g_outstanding > g_peak) g_peak = g_outstanding;
    return (char *)h + HEADER_SIZE;
}

void embark_lvgl_free(void *pointer) {
    if (!pointer) return;
    Header *h = (Header *)((char *)pointer - HEADER_SIZE);
    g_outstanding -= h->size;
    free(h);
}

void *embark_lvgl_realloc(void *pointer, size_t size) {
    if (!pointer) return embark_lvgl_alloc(size);
    if (size == 0) { embark_lvgl_free(pointer); return NULL; }
    Header *h = (Header *)((char *)pointer - HEADER_SIZE);
    void *np = embark_lvgl_alloc(size);
    if (!np) return NULL;
    memcpy(np, pointer, h->size < size ? h->size : size);
    embark_lvgl_free(pointer);
    return np;
}

void embark_lvgl_assert_failed(void) {
    printf("[hooks] LVGL assert failed\n");
    abort();
}

size_t embark_lvgl_outstanding_bytes(void) { return g_outstanding; }
size_t embark_lvgl_peak_bytes(void) { return g_peak; }
size_t embark_lvgl_budget_bytes(void) { return g_budget; }
