/**
 * 平台共用层 · 字体分配作用域（issue 25；ADR 0017 之后是 TTF 栅格化的落点）
 *
 * 字体这边用 lv_mem_alloc 分配字形位图与 dsc，而 lv_mem_alloc 就是本工程自己的钩子
 * （core/config/embark_lvgl_hooks.h）—— 于是"字体的内存从哪来"成了一个作用域问题，不需要改 LVGL：
 *
 *   font_alloc_scope_begin();
 *   lv_tiny_ttf_create_data_ex(ttf, size, 14, 32 * 1024);   // 这段里的分配走字库区
 *   font_alloc_scope_end();
 *
 * 比这更容易漏的是**栅格化期**：tiny_ttf 的字形位图不在 create 时分配，而是第一次绘制
 * 某个字时才 lv_mem_alloc（third_party/lvgl/src/extra/libs/tiny_ttf/lv_tiny_ttf.c 的
 * get_glyph_bitmap）。所以桥把每个实例的 get_glyph_bitmap 包了一层，绘制期再进一次作用域
 * （app/common/aidogui_ui_bridge.cpp 的 scoped_glyph_bitmap）—— 否则一屏几百个字全落 48 KB
 * 的 LVGL 静态池，必爆。
 *
 * 真机：字库区是一块从 PSRAM 显式分配（heap_caps_malloc + MALLOC_CAP_SPIRAM）出来的定容池
 * （device/src/esp32_lvgl_mem.cpp），与 LVGL 的 .bss 静态池分开 —— 于是字体不会
 * 推动 lvgl_peak_bytes，两笔账各管各的（issue 25 验收④）。
 * 宿主：空操作 —— 宿主有真实堆，字体与 LVGL 共用同一个 AllocationCounter，
 * font_alloc_pool_bytes() 返回 0 表示"没有独立字库区"。
 *
 * 作用域不嵌套：实现按"进入即路由、退出即复位"处理（一次建字体 / 一次栅格化里不会再进第二次）。
 */
#ifndef EMBARK_PLATFORM_COMMON_FONT_ALLOC_SCOPE_H
#define EMBARK_PLATFORM_COMMON_FONT_ALLOC_SCOPE_H

#include <cstddef>

namespace embark::platform {

/// 进入/退出字体分配期：这段里 LVGL 的分配走字库区（平台没有字库区时是空操作）。
void font_alloc_scope_begin() noexcept;
void font_alloc_scope_end() noexcept;

/// 字库区容量；0 = 本平台没有独立字库区（宿主），字库分配计入 LVGL 主池。
/// 池是懒建的：还没分配过字形时这里也是 0（真机要等第一次建字体才知道池建没建成）。
[[nodiscard]] std::size_t font_alloc_pool_bytes() noexcept;

/// 字库区的**计划**容量（编译期常量；真机 = esp32_board.h 的 font_psram_bytes，宿主 = 0）。
/// 字形缓存配额要在建字体之前就定下来，那时池还没建，所以查的是计划值而不是实际值。
[[nodiscard]] std::size_t font_alloc_pool_budget_bytes() noexcept;

/// 字库区当前未回收字节（观测用；没有字库区的平台恒为 0）。
[[nodiscard]] std::size_t font_alloc_outstanding_bytes() noexcept;

// --- 常驻字体缓冲（ADR 0017）---------------------------------------------------
// 整份 TTF 要一直躺在内存里：lv_tiny_ttf 只存指针、不复制、也不读文件
// （LV_TINY_TTF_FILE_SUPPORT = 0），栅格化时随取随用。它不是 LVGL 的分配，
// 所以既不走上面的作用域，也不计进 LVGL 池的账 —— 一块活到进程结束的常驻缓冲。
// 真机 = PSRAM（heap_caps_malloc + MALLOC_CAP_SPIRAM），宿主 = malloc；失败返回 nullptr。
[[nodiscard]] void* font_buffer_alloc(std::size_t bytes) noexcept;
void font_buffer_free(void* pointer) noexcept;

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_COMMON_FONT_ALLOC_SCOPE_H */
