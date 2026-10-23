/**
 * 平台共用层 · 定容静态内存池实现（issues/11）
 *
 * 池的物理布局（每个块头也按 16 字节对齐，于是所有负载区都落在 16 字节边界）：
 *
 *   [Block][payload][Block][payload]...
 *    ^16 对齐        ^16 对齐
 *
 * 链表是【地址顺序】的双向链表：释放时向前、向后各并一次，反复分配/释放不会碎片化。
 * 全部计数走纯算术，失败路径不改账 —— 于是错误处理只需要"什么都不做"。
 */
#include "static_pool.h"

#include <cstdint>
#include <cstring>

namespace embark::platform {
namespace {

std::uintptr_t align_up_16(std::uintptr_t value) noexcept {
  return (value + 15U) & ~static_cast<std::uintptr_t>(15U);
}

}  // namespace

StaticPool::StaticPool(void* arena, std::size_t bytes) noexcept {
  if (arena == nullptr) {
    return;
  }
  const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(arena);
  const std::uintptr_t aligned = align_up_16(base);
  const std::size_t lost = static_cast<std::size_t>(aligned - base);
  if (bytes <= lost + header_bytes) {
    return;  // 连一个块头都放不下：capacity_ 保持 0，之后每次都分配失败
  }

  auto* first = reinterpret_cast<Block*>(aligned);
  const std::size_t usable = bytes - lost - header_bytes;
  first->payload_bytes = (usable / granule_bytes) * granule_bytes;
  first->requested_bytes = 0;
  first->prev = nullptr;
  first->next = nullptr;
  first->free = true;

  head_ = first;
  capacity_ = first->payload_bytes;
}

StaticPool::Block* StaticPool::block_of(const void* payload) const noexcept {
  auto* bytes = reinterpret_cast<std::uint8_t*>(const_cast<void*>(payload));
  return reinterpret_cast<Block*>(bytes - header_bytes);
}

bool StaticPool::can_split(const Block& block, std::size_t needed) const noexcept {
  return (block.payload_bytes - needed) >= (header_bytes + granule_bytes);
}

void StaticPool::split(Block& block, std::size_t needed) noexcept {
  if (!can_split(block, needed)) {
    return;  // 尾料不够养一块：整块留给它，免得把池切成一地碎渣
  }
  auto* tail =
      reinterpret_cast<Block*>(reinterpret_cast<std::uint8_t*>(&block) + header_bytes + needed);
  tail->payload_bytes = block.payload_bytes - needed - header_bytes;
  tail->requested_bytes = 0;
  tail->free = true;
  tail->prev = &block;
  tail->next = block.next;
  if (block.next != nullptr) {
    block.next->prev = tail;
  }
  block.next = tail;
  block.payload_bytes = needed;
}

void StaticPool::merge_with_next(Block& block) noexcept {
  Block* tail = block.next;
  if (tail == nullptr || !tail->free) {
    return;
  }
  block.payload_bytes += header_bytes + tail->payload_bytes;
  block.next = tail->next;
  if (tail->next != nullptr) {
    tail->next->prev = &block;
  }
}

void* StaticPool::allocate(std::size_t size) noexcept {
  if (size == 0U) {
    return nullptr;  // 0 字节不是有效请求，不计入失败
  }
  if (head_ == nullptr) {
    ++failures_;
    return nullptr;
  }

  const std::size_t needed = round_up(size);
  for (Block* block = head_; block != nullptr; block = block->next) {
    if (!block->free || block->payload_bytes < needed) {
      continue;
    }
    split(*block, needed);
    block->free = false;
    block->requested_bytes = size;
    outstanding_ += size;
    if (outstanding_ > peak_) {
      peak_ = outstanding_;
    }
    ++allocations_;
    return reinterpret_cast<std::uint8_t*>(block) + header_bytes;
  }

  ++failures_;
  return nullptr;
}

void StaticPool::deallocate(void* pointer) noexcept {
  if (pointer == nullptr || head_ == nullptr) {
    return;
  }
  Block* block = block_of(pointer);
  if (block->free) {
    return;  // 重复释放：无害空操作
  }

  outstanding_ -= block->requested_bytes;
  block->requested_bytes = 0;
  block->free = true;
  ++deallocations_;

  merge_with_next(*block);  // 先并后面
  if (block->prev != nullptr && block->prev->free) {
    merge_with_next(*block->prev);  // 再并前面（并完自己就不在链表里了）
  }
}

void* StaticPool::reallocate(void* pointer, std::size_t size) noexcept {
  if (pointer == nullptr) {
    return allocate(size);
  }
  if (size == 0U) {
    deallocate(pointer);
    return nullptr;
  }

  Block* block = block_of(pointer);
  const std::size_t needed = round_up(size);
  const std::size_t old_size = block->requested_bytes;

  // 1) 原地：把这一个块和它后面**连着**的空闲块加起来够不够。
  //    注意只能"先空算、再动手"：边并边试的话，并完仍不够就没法回滚了
  //    （块头被吞掉，链表已经改了），池会凭空少一大块。
  std::size_t contiguous = block->payload_bytes;
  for (const Block* tail = block->next; tail != nullptr && tail->free; tail = tail->next) {
    contiguous += header_bytes + tail->payload_bytes;
  }
  if (contiguous >= needed) {
    while (block->payload_bytes < needed) {
      merge_with_next(*block);
    }
    split(*block, needed);  // 尾料不够养一块时 split 自己会放弃切分
    outstanding_ = outstanding_ - old_size + size;
    block->requested_bytes = size;
    if (outstanding_ > peak_) {
      peak_ = outstanding_;
    }
    ++reallocations_;
    return pointer;
  }

  // 2) 原地放不下：新分配 + 拷贝 + 还旧；失败时原块原样保留（与 realloc 语义一致）。
  void* moved = allocate(size);
  if (moved == nullptr) {
    return nullptr;  // allocate 已经记过 failures_
  }
  const std::size_t copy_bytes = old_size < size ? old_size : size;
  std::memcpy(moved, pointer, copy_bytes);
  deallocate(pointer);
  ++reallocations_;
  return moved;
}

std::size_t StaticPool::free_block_count() const noexcept {
  std::size_t count = 0;
  for (const Block* block = head_; block != nullptr; block = block->next) {
    if (block->free) {
      ++count;
    }
  }
  return count;
}

}  // namespace embark::platform
