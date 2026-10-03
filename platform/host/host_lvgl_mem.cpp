/**
 * 宿主 · LVGL 内存钩子（issues/05）
 *
 * config/lv_conf.h 里 LV_MEM_CUSTOM=1，把 LVGL 的所有分配都指到这里：
 *   LV_MEM_CUSTOM_ALLOC/FREE/REALLOC → embark_lvgl_alloc/free/realloc
 * 于是"LVGL 用了多少内存"是可测的：每个块前面放一个头（max_align_t 对齐）记住大小，
 * free/realloc 时才能算准未回收字节与峰值。计数逻辑在
 * include/embark/detail/allocation_counter.h（单元测试测的就是那一份）。
 *
 * 记账顺序：先用纯算术判预算，通过了再改账 —— 于是失败路径只需要"把新块还回去"，
 * 不需要任何回滚记账（见下面 alloc / realloc 的注释）。
 *
 * 超预算与分配失败都不在这里 abort，而是返回 nullptr：LVGL 开着 LV_USE_ASSERT_MALLOC
 * （config/lv_conf.h），它会带着【分配点】的文件与行号跳到 embark_lvgl_assert_failed()，
 * 我们再进 embark::fatal —— 这样报错里有调用点，比在分配器里直接 abort 有用得多。
 */
#include "host_lvgl_mem.h"

#include <cstddef>
#include <cstdlib>
#include <new>

#include <embark/detail/allocation_counter.h>
#include <embark/diagnostics.h>
#include <embark_limits.h>
#include <embark_lvgl_hooks.h>
#include <middleware/elog/elog.hpp>

namespace {

/// 分配头：记住块大小，free/realloc 才能准确记账。alignas 保证返回的指针仍然满足
/// 任何类型的对齐要求（malloc 本身按 max_align_t 对齐）。
struct alignas(std::max_align_t) BlockHeader {
  std::size_t size;
};

embark::detail::AllocationCounter g_counter;
bool g_budget_reported = false;

BlockHeader* header_of(void* pointer) noexcept {
  return static_cast<BlockHeader*>(pointer) - 1;
}

/// 如果这次分配之后未回收字节会超过预算：报一次错（只报第一次，免得刷满日志）并拒绝。
bool budget_allows(std::size_t outstanding_after) noexcept {
  if (outstanding_after <= embark::lvgl_alloc_budget_bytes) {
    return true;
  }
  if (!g_budget_reported) {
    g_budget_reported = true;
    ELOG_ERROR("LVGL 堆超预算：这次之后未回收 {} 字节（峰值 {}），预算 {} 字节", outstanding_after,
               g_counter.peak_bytes(), embark::lvgl_alloc_budget_bytes);
  }
  return false;
}

}  // namespace

extern "C" void* embark_lvgl_alloc(size_t size) {
  auto* header = static_cast<BlockHeader*>(std::malloc(sizeof(BlockHeader) + size));
  if (header == nullptr) {
    ELOG_ERROR("LVGL 分配 {} 字节失败（宿主 malloc 返回空）", size);
    return nullptr;
  }
  if (!budget_allows(g_counter.outstanding_bytes() + size)) {
    std::free(header);  // 还没记账，直接还回去就是干净的
    return nullptr;
  }
  header->size = size;
  g_counter.on_alloc(size);
  return header + 1;
}

extern "C" void embark_lvgl_free(void* pointer) {
  if (pointer == nullptr) {
    return;
  }
  BlockHeader* header = header_of(pointer);
  g_counter.on_free(header->size);
  std::free(header);
}

extern "C" void* embark_lvgl_realloc(void* pointer, size_t size) {
  if (pointer == nullptr) {
    return embark_lvgl_alloc(size);
  }
  if (size == 0U) {
    embark_lvgl_free(pointer);
    return nullptr;
  }

  const BlockHeader* const header = header_of(pointer);
  const std::size_t old_size = header->size;

  // 不走 std::realloc：头在返回指针前面，搬移后无法保证头仍然紧贴在数据前。
  auto* moved = static_cast<BlockHeader*>(std::malloc(sizeof(BlockHeader) + size));
  if (moved == nullptr) {
    ELOG_ERROR("LVGL 重整 {} → {} 字节失败（宿主 malloc 返回空）", old_size, size);
    return nullptr;
  }

  // old_size ≤ 未回收字节是这份记账自身的不变量（每个活块都算过一次）。
  const std::size_t outstanding_after = g_counter.outstanding_bytes() - old_size + size;
  if (!budget_allows(outstanding_after)) {
    std::free(moved);
    return nullptr;
  }

  moved->size = size;
  const std::size_t copy_bytes = old_size < size ? old_size : size;
  const auto* source = reinterpret_cast<const unsigned char*>(pointer);
  auto* target = reinterpret_cast<unsigned char*>(moved + 1);
  for (std::size_t index = 0; index < copy_bytes; ++index) {
    target[index] = source[index];
  }

  g_counter.on_realloc(old_size, size);
  std::free(header_of(pointer));
  return moved + 1;
}

extern "C" void embark_lvgl_assert_failed(void) {
  ELOG_ERROR("LVGL 断言失败（LV_ASSERT / LV_ASSERT_MSG）");
  embark::fatal("LVGL 断言失败");
}

namespace embark::platform::host {

std::size_t lvgl_outstanding_bytes() noexcept { return g_counter.outstanding_bytes(); }

std::size_t lvgl_peak_bytes() noexcept { return g_counter.peak_bytes(); }

std::size_t lvgl_allocations() noexcept { return g_counter.allocations(); }

}  // namespace embark::platform::host
