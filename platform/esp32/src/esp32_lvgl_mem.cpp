/**
 * @file esp32_lvgl_mem.cpp
 * @brief LVGL 的内存与断言钩子（真机实现：定容静态池，不碰堆）。
 *
 * 钩子本身是 C 契约（config/embark_lvgl_hooks.h，被 config/lv_conf.h 的
 * LV_MEM_CUSTOM_INCLUDE / LV_ASSERT_HANDLER_INCLUDE 指到这里），所以文件末尾
 * 那几个函数必须留在全局作用域、不带命名空间。
 *
 * 与宿主版的差异（宿主见 platform/host/host_lvgl_mem.cpp）：
 *   - 宿主：malloc + AllocationCounter + 预算检查（超预算拒绝分配）；
 *   - 真机：StaticPool（.bss 里 48 KB 的池）——池本身就是硬预算，满了返回 nullptr。
 * 两条纪律一致：分配失败不回滚、不 abort，把"失败"交给 LVGL 的断言报出分配点；
 * 池用尽只报一行日志（一次 OOM 不该刷满串口）。
 *
 * LVGL 只在唯一 UI 任务里被触碰（spec §6），所以这里不需要加锁。
 */
#include "esp32_lvgl_mem.h"

#include <cstdint>

#include <embark/diagnostics.h>
#include <embark_lvgl_hooks.h>
#include <middleware/elog/elog.hpp>

#include "esp32_board.h"
#include "static_pool.h"

namespace embark::platform::esp32 {
namespace {

/// 池太小的话连一屏控件都撑不住：宁可编译不过，也别到现场才发现。
static_assert(lvgl_pool_bytes >= 8U * 1024U,
              "LVGL 静态池至少给 8 KB（esp32_board.h 的 lvgl_pool_bytes）");

/// LVGL 的堆就是这块 .bss：不碰 heap_caps、不沾 PSRAM，容量在编译期就定死。
/// alignas(16) 是为了最坏情况的对齐（StaticPool 内部也按 16 字节粒度切）。
alignas(16) std::uint8_t g_lvgl_pool_arena[lvgl_pool_bytes];

/// 用尽只报一次（含容量/未回收/峰值，够定位"哪个页面没删控件"）。
bool g_exhausted_reported = false;

}  // namespace

/// LVGL 的定容池。**故意不放匿名命名空间**：文件末尾全局作用域的 C 钩子要够得着。
StaticPool g_lvgl_pool(g_lvgl_pool_arena, sizeof(g_lvgl_pool_arena));

/// 池用尽时的一行日志（钩子里调用，够得着就行）。
void lvgl_report_exhausted() noexcept {
  if (g_exhausted_reported) {
    return;
  }
  g_exhausted_reported = true;
  ELOG_ERROR("LVGL 静态堆用尽：容量 {} 字节，未回收 {} 字节，峰值 {} 字节（先看是不是有控件没删）",
             static_cast<unsigned>(g_lvgl_pool.capacity_bytes()),
             static_cast<unsigned>(g_lvgl_pool.outstanding_bytes()),
             static_cast<unsigned>(g_lvgl_pool.peak_bytes()));
}

std::size_t lvgl_pool_capacity_bytes() noexcept {
  return g_lvgl_pool.capacity_bytes();
}

std::size_t lvgl_outstanding_bytes() noexcept {
  return g_lvgl_pool.outstanding_bytes();
}

std::size_t lvgl_peak_bytes() noexcept {
  return g_lvgl_pool.peak_bytes();
}

std::size_t lvgl_allocations() noexcept {
  return g_lvgl_pool.allocations();
}

std::size_t lvgl_failures() noexcept {
  return g_lvgl_pool.failures();
}

std::size_t lvgl_free_block_count() noexcept {
  return g_lvgl_pool.free_block_count();
}

}  // namespace embark::platform::esp32

// --- LVGL 的 C 契约（全局作用域；这些名字不带命名空间）---------------------------

extern "C" void* embark_lvgl_alloc(size_t size) {
  void* memory = embark::platform::esp32::g_lvgl_pool.allocate(size);
  if (memory == nullptr && size > 0U) {
    embark::platform::esp32::lvgl_report_exhausted();
  }
  return memory;
}

extern "C" void embark_lvgl_free(void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  embark::platform::esp32::g_lvgl_pool.deallocate(pointer);
}

extern "C" void* embark_lvgl_realloc(void* pointer, size_t size) {
  // StaticPool::reallocate 自己处理这三种边界：nullptr → 新分配、size 0 → 归还并返回空、
  // 原地放不下 → 搬家（内容照样保留）。
  void* memory = embark::platform::esp32::g_lvgl_pool.reallocate(pointer, size);
  if (memory == nullptr && size > 0U) {
    embark::platform::esp32::lvgl_report_exhausted();
  }
  return memory;
}

extern "C" void embark_lvgl_assert_failed(void) {
  ELOG_ERROR("LVGL 断言失败（LV_ASSERT / LV_ASSERT_MSG）");
  embark::fatal("LVGL 断言失败");
}

extern "C" size_t embark_lvgl_outstanding_bytes(void) {
  return embark::platform::esp32::g_lvgl_pool.outstanding_bytes();
}

extern "C" size_t embark_lvgl_peak_bytes(void) {
  return embark::platform::esp32::g_lvgl_pool.peak_bytes();
}

extern "C" size_t embark_lvgl_budget_bytes(void) {
  // 真机口径：预算就是池容量本身（宿主那边是 256 KB 的软预算，这里池外没有第二个堆）。
  return embark::platform::esp32::lvgl_pool_bytes;
}
