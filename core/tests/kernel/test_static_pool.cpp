/**
 * 内核测试：定容静态内存池（issues/11）
 *
 * StaticPool 是"真机上的 LVGL 堆"：宿主 LVGL 用 malloc + 全局计数器，真机只能用
 * .bss 里的定容池（零堆，spec §10）。这一份测试就是它的契约：
 *   - 16 字节对齐、池满返回 nullptr 且计一次失败；
 *   - 释放后能复用、相邻空闲块双向合并（反复分配/释放不碎片化）；
 *   - realloc 能原地就原地（地址不变），要搬家就保留内容，失败保留原块；
 *   - 统计口径（outstanding/peak/allocations/reallocations/failures）与边界（0/nullptr）。
 *
 * 注意：池本身不分配内存 —— 测试里的 arena 全在栈上，所以这份测试不依赖任何后端。
 */
#include <cstddef>
#include <cstdint>

#include <doctest/doctest.h>

#include "static_pool.h"

namespace {

using embark::platform::StaticPool;

/// 池的负载区：16 字节对齐，够几轮"分配 → 释放 → 再分配"的折腾。
struct Arena {
  alignas(16) unsigned char bytes[1024] = {};
};

bool is_aligned_16(const void* pointer) {
  return (reinterpret_cast<std::uintptr_t>(pointer) % 16U) == 0U;
}

}  // namespace

TEST_CASE("StaticPool：分配对齐与基本记账") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  CHECK(pool.capacity_bytes() > 0U);
  CHECK(pool.outstanding_bytes() == 0U);
  CHECK(pool.free_block_count() == 1U);

  void* small = pool.allocate(1U);
  REQUIRE(small != nullptr);
  CHECK(is_aligned_16(small));
  CHECK(pool.outstanding_bytes() == 1U);
  CHECK(pool.allocations() == 1U);

  void* medium = pool.allocate(20U);
  REQUIRE(medium != nullptr);
  CHECK(is_aligned_16(medium));
  CHECK(medium != small);
  CHECK(pool.outstanding_bytes() == 21U);
  CHECK(pool.peak_bytes() == 21U);

  // 负载区不能互相踩：往一个块里写满，另一个块的首字节不能被改掉。
  auto* small_bytes = static_cast<unsigned char*>(small);
  small_bytes[0] = 0xAAU;  // 只写自己的第 0 字节（对齐后前 16 字节都是它的负载）
  CHECK(static_cast<unsigned char*>(medium)[0] == 0x00U);

  pool.deallocate(medium);
  pool.deallocate(small);
  CHECK(pool.outstanding_bytes() == 0U);
  CHECK(pool.deallocations() == 2U);
  CHECK(pool.peak_bytes() == 21U);  // 峰值不随释放回落
  CHECK(pool.free_block_count() == 1U);
}

TEST_CASE("StaticPool：池满返回 nullptr 并计一次失败") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  void* whole = pool.allocate(pool.capacity_bytes());
  REQUIRE(whole != nullptr);
  CHECK(pool.outstanding_bytes() == pool.capacity_bytes());
  CHECK(pool.failures() == 0U);

  CHECK(pool.allocate(16U) == nullptr);
  CHECK(pool.failures() == 1U);
  CHECK(pool.allocate(pool.capacity_bytes() + 16U) == nullptr);
  CHECK(pool.failures() == 2U);

  // 失败不破坏已有分配：把整块还回去之后还能照样拿回来。
  pool.deallocate(whole);
  CHECK(pool.outstanding_bytes() == 0U);
  CHECK(pool.free_block_count() == 1U);
  void* again = pool.allocate(pool.capacity_bytes());
  CHECK(again == whole);
}

TEST_CASE("StaticPool：释放后复用与相邻块双向合并") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  void* first = pool.allocate(64U);
  void* second = pool.allocate(64U);
  void* third = pool.allocate(64U);
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  REQUIRE(third != nullptr);

  // 先还中间那块：两块空闲还没挨着，仍是 2 个空闲块。
  pool.deallocate(second);
  CHECK(pool.free_block_count() == 2U);

  // 再还第一块：向后合并把 second 吞进来 —— first+second 成为一整块。
  // （池尾那块从未被分配过的空闲区还在，所以计数是"合并区 + 池尾" = 2。）
  pool.deallocate(first);
  CHECK(pool.free_block_count() == 2U);
  CHECK(pool.outstanding_bytes() == 64U);  // 只剩 third

  // 合并出来的 128+ 字节区域要能一次吃掉一个 100 字节请求（不合并就分不出来）。
  void* merged = pool.allocate(100U);
  CHECK(merged == first);

  // 换一种顺序：先还前面的、再还后面的，也要合并（向前合并那条路径）。
  StaticPool other(arena.bytes, sizeof(arena.bytes));
  void* left = other.allocate(64U);
  void* right = other.allocate(64U);
  REQUIRE(left != nullptr);
  REQUIRE(right != nullptr);
  other.deallocate(left);
  other.deallocate(right);
  CHECK(other.free_block_count() == 1U);
  CHECK(other.outstanding_bytes() == 0U);
}

TEST_CASE("StaticPool：realloc 能原地就原地") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  void* block = pool.allocate(32U);
  REQUIRE(block != nullptr);
  auto* bytes = static_cast<unsigned char*>(block);
  for (std::size_t index = 0; index < 32U; ++index) {
    bytes[index] = static_cast<unsigned char>(index);
  }

  // 池尾部还空着：扩到 40 字节应当原地完成（地址不变、内容不变）。
  void* grown = pool.reallocate(block, 40U);
  CHECK(grown == block);
  CHECK(pool.reallocations() == 1U);
  CHECK(pool.outstanding_bytes() == 40U);
  for (std::size_t index = 0; index < 32U; ++index) {
    CHECK(static_cast<unsigned char*>(grown)[index] == static_cast<unsigned char>(index));
  }

  // 原地缩小也不搬家。
  CHECK(pool.reallocate(grown, 8U) == grown);
  CHECK(pool.outstanding_bytes() == 8U);
}

TEST_CASE("StaticPool：realloc 要搬家时保留内容") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  void* front = pool.allocate(16U);
  REQUIRE(front != nullptr);
  auto* front_bytes = static_cast<unsigned char*>(front);
  for (std::size_t index = 0; index < 16U; ++index) {
    front_bytes[index] = static_cast<unsigned char>(0x30U + index);
  }
  // 紧跟其后的块是"用着"的：front 想长大就只能搬家。
  void* blocker = pool.allocate(16U);
  REQUIRE(blocker != nullptr);

  void* moved = pool.reallocate(front, 128U);
  REQUIRE(moved != nullptr);
  CHECK(moved != front);
  for (std::size_t index = 0; index < 16U; ++index) {
    CHECK(static_cast<unsigned char*>(moved)[index] == static_cast<unsigned char>(0x30U + index));
  }
  CHECK(pool.reallocations() == 1U);
  CHECK(pool.outstanding_bytes() == 128U + 16U);  // moved + blocker

  // 搬家后的旧块必须真的还回池子：把剩下的都还干净，池要回到"一整块空闲"
  // （这是"不碎片化"的直接证据：反复分配/释放不该留下永远的洞）。
  pool.deallocate(moved);
  pool.deallocate(blocker);
  CHECK(pool.outstanding_bytes() == 0U);
  CHECK(pool.free_block_count() == 1U);
  CHECK(pool.allocate(pool.capacity_bytes()) != nullptr);
}

TEST_CASE("StaticPool：realloc 失败时保留原块") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  void* block = pool.allocate(16U);
  REQUIRE(block != nullptr);
  static_cast<unsigned char*>(block)[0] = 0x5AU;

  // 比整个池还大：分配不出来，原块必须原样保留（与 realloc 语义一致）。
  CHECK(pool.reallocate(block, pool.capacity_bytes() + 16U) == nullptr);
  CHECK(pool.failures() == 1U);
  CHECK(pool.outstanding_bytes() == 16U);
  CHECK(static_cast<unsigned char*>(block)[0] == 0x5AU);

  // realloc(nullptr, n) 等价 allocate(n)；realloc(p, 0) 等价 free(p)。
  void* fresh = pool.reallocate(nullptr, 24U);
  CHECK(fresh != nullptr);
  CHECK(pool.reallocate(fresh, 0U) == nullptr);
  CHECK(pool.outstanding_bytes() == 16U);
}

TEST_CASE("StaticPool：边界与统计口径") {
  Arena arena;
  StaticPool pool(arena.bytes, sizeof(arena.bytes));

  // allocate(0)：无效请求，不是失败。
  CHECK(pool.allocate(0U) == nullptr);
  CHECK(pool.failures() == 0U);
  CHECK(pool.allocations() == 0U);

  // deallocate(nullptr) 无害；重复释放同一个指针不重复记账。
  pool.deallocate(nullptr);
  CHECK(pool.deallocations() == 0U);

  void* block = pool.allocate(48U);
  REQUIRE(block != nullptr);
  pool.deallocate(block);
  CHECK(pool.deallocations() == 1U);
  CHECK(pool.outstanding_bytes() == 0U);
  pool.deallocate(block);  // 第二次：空操作
  CHECK(pool.deallocations() == 1U);
  CHECK(pool.outstanding_bytes() == 0U);

  // 池小到连一个块头都放不下：每次分配都失败，但不崩。
  alignas(16) unsigned char tiny[16] = {};
  StaticPool too_small(tiny, sizeof(tiny));
  CHECK(too_small.capacity_bytes() == 0U);
  CHECK(too_small.allocate(1U) == nullptr);
  CHECK(too_small.failures() == 1U);
  too_small.deallocate(nullptr);  // 空池上的空操作

  // 空指针构造：不允许崩，也不允许有可用容量。
  StaticPool empty(nullptr, 0U);
  CHECK(empty.capacity_bytes() == 0U);
  CHECK(empty.allocate(8U) == nullptr);
}
