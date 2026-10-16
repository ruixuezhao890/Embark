/**
 * 平台共用层 · 字库分配作用域（issue 25）
 *
 * 运行时字库（lv_font_load）内部用 lv_mem_alloc 分配字形位图与 dsc，而 lv_mem_alloc 就是
 * 本工程自己的钩子（config/embark_lvgl_hooks.h）—— 于是"字库的内存从哪来"成了一个作用域
 * 问题，不需要改 LVGL：
 *
 *   font_alloc_scope_begin();
 *   lv_font_load("F:/ui_font_ui_heiti_14.bin");   // 这段里的分配走字库区
 *   font_alloc_scope_end();
 *
 * 真机：字库区是一块从 PSRAM 显式分配（heap_caps_malloc + MALLOC_CAP_SPIRAM）出来的定容池
 * （platform/esp32/src/esp32_lvgl_mem.cpp），与 LVGL 的 .bss 静态池分开 —— 于是加载字库不会
 * 推动 lvgl_peak_bytes，两笔账各管各的（issue 25 验收④）。
 * 宿主：空操作 —— 宿主有真实堆，字库与 LVGL 共用同一个 AllocationCounter，
 * font_alloc_pool_bytes() 返回 0 表示"没有独立字库区"。
 *
 * 作用域不嵌套：实现按"进入即路由、退出即复位"处理（字库加载不会触发第二次加载）。
 */
#ifndef EMBARK_PLATFORM_COMMON_FONT_ALLOC_SCOPE_H
#define EMBARK_PLATFORM_COMMON_FONT_ALLOC_SCOPE_H

#include <cstddef>

namespace embark::platform {

/// 进入/退出字库加载期：这段里 LVGL 的分配走字库区（平台没有字库区时是空操作）。
void font_alloc_scope_begin() noexcept;
void font_alloc_scope_end() noexcept;

/// 字库区容量；0 = 本平台没有独立字库区（宿主），字库分配计入 LVGL 主池。
[[nodiscard]] std::size_t font_alloc_pool_bytes() noexcept;

/// 字库区当前未回收字节（观测用；没有字库区的平台恒为 0）。
[[nodiscard]] std::size_t font_alloc_outstanding_bytes() noexcept;

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_COMMON_FONT_ALLOC_SCOPE_H */
