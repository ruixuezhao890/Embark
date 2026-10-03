/**
 * 宿主 · LVGL 内存观测（issues/05）
 *
 * 分配/释放本身在 host_lvgl_mem.cpp —— 它实现 config/embark_lvgl_hooks.h 的 C 钩子
 * （config/lv_conf.h 用 LV_MEM_CUSTOM=1 把 LVGL 的 malloc/free/realloc 指过来）。
 * 这里只导出"用了多少"：演示程序退出前打印一行，验收时对着 lvgl_alloc_budget_bytes 看。
 *
 * 记账实现在 include/embark/detail/allocation_counter.h —— 单元测试测的就是那一份。
 */
#ifndef EMBARK_PLATFORM_HOST_LVGL_MEM_H
#define EMBARK_PLATFORM_HOST_LVGL_MEM_H

#include <cstddef>

namespace embark::platform::host {

/// LVGL 当前未回收字节。注意：LV_MEM_CUSTOM=1 时 LVGL 没有 lv_deinit（见 lvgl_port.cpp），
/// 所以进程退出前它不会归零 —— 它测的是"活着的 LVGL 对象一共占了多少"。
[[nodiscard]] std::size_t lvgl_outstanding_bytes() noexcept;

/// 历史峰值未回收字节：与 config/embark_limits.h 的 lvgl_alloc_budget_bytes 对照。
[[nodiscard]] std::size_t lvgl_peak_bytes() noexcept;

/// LVGL 的分配次数（峰值与它一起看：次数多而峰值小说明反复申请释放）。
[[nodiscard]] std::size_t lvgl_allocations() noexcept;

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_LVGL_MEM_H */
