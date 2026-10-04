/**
 * 平台共用层 · 定容静态内存池（issues/11）
 *
 * 给"必须有界"的消费者用的分配器（真机上是 LVGL 的堆，见 platform/esp32/src/esp32_lvgl_mem.cpp）：
 *
 *   - 池内存由调用方提供：真机是 .bss 里的静态数组，单元测试用自己的数组 ——
 *     本类自己不做任何分配（无堆、无异常，符合 spec §10）。
 *   - 16 字节粒度：返回的指针一律 16 字节对齐（LVGL 只要求 4/8，留余量给 DMA）。
 *   - 首次适配 + 双向相邻合并：释放后前后空闲块会并回一块，反复分配/释放不会碎片化到
 *     "明明有总量却分不出小块"。
 *   - 池满返回 nullptr，由调用方决定怎么报错（LVGL 会带着分配点跳进断言钩子）。
 *
 * 观测项（outstanding/peak/allocations/failures）是验收与看板用的：真机没有宿主那套
 * 全局 new/delete 钩子，"零堆"这条只能靠"池里还剩多少"来证明。
 *
 * 线程纪律：不加锁。宿主侧 LVGL 只跑在唯一 UI 任务里，真机同理（spec §5、§10）。
 */
#ifndef EMBARK_PLATFORM_STATIC_POOL_H
#define EMBARK_PLATFORM_STATIC_POOL_H

#include <cstddef>

namespace embark::platform {

class StaticPool {
 public:
  /// arena 必须指向 bytes 字节可写内存；池会把起点向上对齐到 16 字节。
  StaticPool(void* arena, std::size_t bytes) noexcept;

  StaticPool(const StaticPool&) = delete;
  StaticPool& operator=(const StaticPool&) = delete;

  /// 分配 size 字节；池满（或 size == 0）返回 nullptr。
  [[nodiscard]] void* allocate(std::size_t size) noexcept;

  /// 归还指针；nullptr 与"重复释放同一个指针"都是无害的空操作。
  void deallocate(void* pointer) noexcept;

  /// 重整：能原地扩就原地扩（地址不变），否则搬家并保留内容（超出新长度的部分丢弃）。
  [[nodiscard]] void* reallocate(void* pointer, std::size_t size) noexcept;

  // --- 观测 -----------------------------------------------------------------
  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t outstanding_bytes() const noexcept { return outstanding_; }
  [[nodiscard]] std::size_t peak_bytes() const noexcept { return peak_; }
  [[nodiscard]] std::size_t allocations() const noexcept { return allocations_; }
  [[nodiscard]] std::size_t reallocations() const noexcept { return reallocations_; }
  [[nodiscard]] std::size_t deallocations() const noexcept { return deallocations_; }
  [[nodiscard]] std::size_t failures() const noexcept { return failures_; }
  /// 当前空闲块个数（相邻空闲块已合并，理想情况下是 1）。测试用。
  [[nodiscard]] std::size_t free_block_count() const noexcept;

  /// 单次分配的最小粒度与对齐：写测试断言时用。
  static constexpr std::size_t granule_bytes = 16U;

 private:
  /// 块头：地址顺序双向链表贯穿整个池，合并时不用再遍历找前驱。
  struct Block {
    std::size_t payload_bytes;    // 本块可用负载（ram 的倍数）
    std::size_t requested_bytes;  // 申请者要的字节数（统计口径用这个）
    Block* prev;
    Block* next;
    bool free;
  };

  static constexpr std::size_t round_up(std::size_t value) noexcept {
    return (value + (granule_bytes - 1U)) & ~(granule_bytes - 1U);
  }
  /// 块头本身也占 16 字节的整数倍，于是每个负载区都落在 16 字节边界上。
  /// （这里不能再调 round_up()：类内静态成员的初始化器早于成员函数"定义完成"。）
  static constexpr std::size_t header_bytes =
      (sizeof(Block) + (granule_bytes - 1U)) & ~(granule_bytes - 1U);

  Block* block_of(const void* payload) const noexcept;
  [[nodiscard]] bool can_split(const Block& block, std::size_t needed) const noexcept;
  void split(Block& block, std::size_t needed) noexcept;
  void merge_with_next(Block& block) noexcept;

  Block* head_ = nullptr;     // 池首块（地址最小）
  std::size_t capacity_ = 0;  // 可分配的负载总字节（已扣掉块头与对齐损耗）
  std::size_t outstanding_ = 0;
  std::size_t peak_ = 0;
  std::size_t allocations_ = 0;
  std::size_t reallocations_ = 0;
  std::size_t deallocations_ = 0;
  std::size_t failures_ = 0;
};

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_STATIC_POOL_H */
