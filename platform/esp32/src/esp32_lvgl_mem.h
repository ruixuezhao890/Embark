/**
 * @file esp32_lvgl_mem.h
 * @brief 真机 LVGL 内存观测（定容静态池的账本）。
 *
 * 宿主版在 platform/host/host_lvgl_mem.{h,cpp}（malloc 包装 + 计数器）；
 * 真机版不用堆：LVGL 的堆就是 .bss 里那块 `lvgl_pool_bytes` 的静态池
 * （platform/common/static_pool.h），池满就返回 nullptr，由 LVGL 的
 * LV_USE_ASSERT_MALLOC 带着分配点跳进 embark_lvgl_assert_failed()。
 *
 * 为什么要有观测接口：真机没有宿主那套全局 new/delete 钩子，"零堆"这条纪律
 * 只能靠分配器自己的账本来证明（spec §10 的对象总量上限）。
 */
#ifndef EMBARK_PLATFORM_ESP32_LVGL_MEM_H
#define EMBARK_PLATFORM_ESP32_LVGL_MEM_H

#include <cstddef>

namespace embark::platform::esp32 {

/// LVGL 静态池的容量（= esp32_board.h 的 lvgl_pool_bytes）。
[[nodiscard]] std::size_t lvgl_pool_capacity_bytes() noexcept;
/// 当前未回收字节。
[[nodiscard]] std::size_t lvgl_outstanding_bytes() noexcept;
/// 历史峰值字节。
[[nodiscard]] std::size_t lvgl_peak_bytes() noexcept;
/// 累计分配次数。
[[nodiscard]] std::size_t lvgl_allocations() noexcept;
/// 累计失败次数（池满；超预算的另一种说法）。
[[nodiscard]] std::size_t lvgl_failures() noexcept;
/// 空闲块数量：长期不下降就说明碎片化（反复建/删控件要盯这个数）。
[[nodiscard]] std::size_t lvgl_free_block_count() noexcept;

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_LVGL_MEM_H */
