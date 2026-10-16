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

#include <cstddef>
#include <cstdint>
#include <new>

#include <esp_heap_caps.h>

#include <embark/diagnostics.h>
#include <embark_lvgl_hooks.h>
#include <middleware/elog/elog.hpp>

#include "esp32_board.h"
#include "font_alloc_scope.h"
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

// --- 字库区（issue 25）--------------------------------------------------------
// 运行时字库（lv_font_load）的分配走另一块池：从 PSRAM 显式分配（heap_caps_malloc +
// MALLOC_CAP_SPIRAM），与上面 .bss 里的 LVGL 静态池分开。两笔账于是各管各的：加载字库
// 不推动 lvgl_peak_bytes，字库的字节预算由 FontLedger 管（issue 25 验收④）。
// 池是懒创建的：没人加载字库就不占这块 PSRAM；StaticPool 没有默认构造，所以用一块
// 静态存储 + placement new（构造本身不需要堆，也不会抛异常）。
namespace {

alignas(StaticPool) std::uint8_t g_font_pool_storage[sizeof(StaticPool)];
std::uint8_t* g_font_arena = nullptr;  // PSRAM 里那块；判断指针归属要用它
std::size_t g_font_arena_bytes = 0;
StaticPool* g_font_pool = nullptr;
bool g_font_scope_active = false;  // 是否正处在 lv_font_load / lv_font_free 里
bool g_font_pool_failed = false;   // 分配失败过就别每次重试（日志已经报过）
bool g_font_exhausted_reported = false;

}  // namespace

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

/// 字库区，懒创建；PSRAM 分配不出来就返回 nullptr（调用方退回 LVGL 主池）。
StaticPool* font_pool() noexcept {
  if (g_font_pool != nullptr) {
    return g_font_pool;
  }
  if (g_font_pool_failed || font_psram_bytes == 0U) {
    return nullptr;
  }
  void* arena = heap_caps_malloc(font_psram_bytes, MALLOC_CAP_SPIRAM);
  if (arena == nullptr) {
    g_font_pool_failed = true;
    ELOG_ERROR("字库区：从 PSRAM 分配 {} 字节失败 —— 字库分配退回 LVGL 静态池",
               static_cast<unsigned>(font_psram_bytes));
    return nullptr;
  }
  g_font_arena = static_cast<std::uint8_t*>(arena);
  g_font_arena_bytes = font_psram_bytes;
  g_font_pool = new (g_font_pool_storage) StaticPool(g_font_arena, g_font_arena_bytes);
  ELOG_INFO("字库区就绪：PSRAM {} 字节（与 LVGL 静态池 {} 字节分开记账）",
            static_cast<unsigned>(g_font_pool->capacity_bytes()),
            static_cast<unsigned>(g_lvgl_pool.capacity_bytes()));
  return g_font_pool;
}

/// 字库加载期的分配目标：在作用域内且字库区可用 → 字库区，否则 LVGL 主池。
StaticPool& active_pool() noexcept {
  if (g_font_scope_active) {
    StaticPool* pool = font_pool();
    if (pool != nullptr) {
      return *pool;
    }
  }
  return g_lvgl_pool;
}

/// 归还时的归属：按地址落在哪块池里决定还到哪 —— 不依赖"当前作用域"，于是作用域
/// 边界与块的生命期不一致（字库活着、作用域已退出）时也不会还给错误的池。
StaticPool& pool_of(void* pointer) noexcept {
  const auto address = reinterpret_cast<std::uintptr_t>(pointer);
  const auto base = reinterpret_cast<std::uintptr_t>(g_font_arena);
  if (g_font_arena != nullptr && address >= base && address < base + g_font_arena_bytes) {
    return *g_font_pool;
  }
  return g_lvgl_pool;
}

void font_alloc_scope_set(bool active) noexcept {
  g_font_scope_active = active;
}

std::size_t font_pool_capacity_bytes() noexcept {
  return g_font_pool != nullptr ? g_font_pool->capacity_bytes() : 0U;
}

std::size_t font_pool_outstanding_bytes() noexcept {
  return g_font_pool != nullptr ? g_font_pool->outstanding_bytes() : 0U;
}

/// 字库区用尽（字库本身比池大，或同时加载了太多份）：报一次就够。
void font_report_exhausted() noexcept {
  if (g_font_exhausted_reported || g_font_pool == nullptr) {
    return;
  }
  g_font_exhausted_reported = true;
  ELOG_ERROR(
      "字库区用尽：容量 {} 字节，未回收 {} 字节，峰值 {} 字节（对一下 font_budget_bytes "
      "与字库大小）",
      static_cast<unsigned>(g_font_pool->capacity_bytes()),
      static_cast<unsigned>(g_font_pool->outstanding_bytes()),
      static_cast<unsigned>(g_font_pool->peak_bytes()));
}

}  // namespace embark::platform::esp32

// --- LVGL 的 C 契约（全局作用域；这些名字不带命名空间）---------------------------

extern "C" void* embark_lvgl_alloc(size_t size) {
  // 字库加载期（font_alloc_scope_begin/end）分配走 PSRAM 里那块字库区，其余走 .bss 主池。
  embark::platform::StaticPool& pool = embark::platform::esp32::active_pool();
  void* memory = pool.allocate(size);
  if (memory == nullptr && size > 0U) {
    if (&pool == &embark::platform::esp32::g_lvgl_pool) {
      embark::platform::esp32::lvgl_report_exhausted();
    } else {
      embark::platform::esp32::font_report_exhausted();
    }
  }
  return memory;
}

extern "C" void embark_lvgl_free(void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  embark::platform::esp32::pool_of(pointer).deallocate(pointer);
}

extern "C" void* embark_lvgl_realloc(void* pointer, size_t size) {
  // StaticPool::reallocate 自己处理这三种边界：nullptr → 新分配、size 0 → 归还并返回空、
  // 原地放不下 → 搬家（内容照样保留）。ptr == nullptr 时按"当前作用域"选池（等价于 alloc），
  // 否则按块的归属选池 —— 与 free 同一套判断。
  embark::platform::StaticPool& pool = (pointer == nullptr)
                                           ? embark::platform::esp32::active_pool()
                                           : embark::platform::esp32::pool_of(pointer);
  void* memory = pool.reallocate(pointer, size);
  if (memory == nullptr && size > 0U) {
    if (&pool == &embark::platform::esp32::g_lvgl_pool) {
      embark::platform::esp32::lvgl_report_exhausted();
    } else {
      embark::platform::esp32::font_report_exhausted();
    }
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

// --- 字库分配作用域（issue 25）：真机侧 = 切到 PSRAM 那块字库区 -------------------
// 只有"路由开关"在这里；字库怎么加载、账怎么记，都在 platform/common/font_service.cpp
// （宿主与真机同一份）。
namespace embark::platform {

void font_alloc_scope_begin() noexcept {
  esp32::font_alloc_scope_set(true);
}

void font_alloc_scope_end() noexcept {
  esp32::font_alloc_scope_set(false);
}

std::size_t font_alloc_pool_bytes() noexcept {
  return esp32::font_pool_capacity_bytes();  // 池是懒建的：没加载过就是 0
}

std::size_t font_alloc_outstanding_bytes() noexcept {
  return esp32::font_pool_outstanding_bytes();
}

}  // namespace embark::platform
