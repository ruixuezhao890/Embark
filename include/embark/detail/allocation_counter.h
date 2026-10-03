/**
 * 分配计数（宿主 LVGL 内存钩子与单元测试共用）
 *
 * 只记账：不分配、不打印、不做任何策略决定。宿主侧的 malloc 包装
 * （platform/host/host_lvgl_mem.cpp）与单元测试用同一份实现，于是
 * 「LVGL 对象总量上限」这个判据（spec §10）只有一处定义，测试测的就是线上跑的那份。
 *
 * 为什么需要它是类而不是几个全局变量：host_lvgl_mem.cpp 要同时统计
 * 未回收字节、峰值与调用次数，测试则要能随意造/复位计数器；做成值类型
 * 两边都好用，也避免动到全局状态。
 */
#ifndef EMBARK_DETAIL_ALLOCATION_COUNTER_H
#define EMBARK_DETAIL_ALLOCATION_COUNTER_H

#include <cstddef>

namespace embark::detail {

/// 分配记账：未回收字节、峰值、各类调用次数。
class AllocationCounter {
 public:
  void on_alloc(std::size_t size) noexcept {
    ++allocations_;
    grow(size);
  }

  void on_free(std::size_t size) noexcept {
    ++frees_;
    shrink(size);
  }

  /// realloc 语义：记账时看作「改大小」，不计进 alloc/free 次数（它们各自另有计数）。
  void on_realloc(std::size_t old_size, std::size_t new_size) noexcept {
    ++reallocations_;
    if (new_size >= old_size) {
      grow(new_size - old_size);
    } else {
      shrink(old_size - new_size);
    }
  }

  /// 未回收字节数（当前还活着的分配）。
  [[nodiscard]] std::size_t outstanding_bytes() const noexcept { return outstanding_; }

  /// 历史峰值未回收字节数。
  [[nodiscard]] std::size_t peak_bytes() const noexcept { return peak_; }

  [[nodiscard]] std::size_t allocations() const noexcept { return allocations_; }
  [[nodiscard]] std::size_t frees() const noexcept { return frees_; }
  [[nodiscard]] std::size_t reallocations() const noexcept { return reallocations_; }

  /// 是否已经超出预算（严格大于；等于预算算没超）。只回答，不裁决。
  [[nodiscard]] bool over_budget(std::size_t budget) const noexcept {
    return outstanding_ > budget;
  }

  void reset() noexcept { *this = AllocationCounter{}; }

 private:
  void grow(std::size_t delta) noexcept {
    outstanding_ += delta;
    if (outstanding_ > peak_) {
      peak_ = outstanding_;
    }
  }

  void shrink(std::size_t delta) noexcept {
    // 释放对不上时不要回绕成一个巨大的数：夹到 0，让调用方看到 outstanding 归零。
    outstanding_ = (delta > outstanding_) ? 0U : outstanding_ - delta;
  }

  std::size_t outstanding_ = 0;
  std::size_t peak_ = 0;
  std::size_t allocations_ = 0;
  std::size_t frees_ = 0;
  std::size_t reallocations_ = 0;
};

}  // namespace embark::detail

#endif  // EMBARK_DETAIL_ALLOCATION_COUNTER_H
