// 全局 operator new/delete 重载定义（zero_alloc_hooks.h 的落地）。
//
// 重载全家桶：普通 / 数组 / C++17 对齐（align_val_t）各一对，delete 再带
// sized 变体（C++14 起编译器可能选用 sized 释放）。全部只计数 new 次数，
// 分配/释放照常走 CRT（malloc/free / aligned_alloc）—— 行为与默认一致，
// 只是多了记账，所以可以放心地在整个测试可执行文件里生效。
#include "zero_alloc_hooks.h"

#include <malloc.h>  // _aligned_malloc / _aligned_free（MinGW 的 <cstdlib> 没有 std::aligned_alloc）
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

std::uint32_t g_allocations = 0;  // BSS：进程启动即 0，不用构造函数。

}  // namespace

namespace embark::test {

void reset_global_allocation_counters() noexcept {
  g_allocations = 0;
}

std::uint32_t global_allocation_count() noexcept {
  return g_allocations;
}

}  // namespace embark::test

// —— 普通分配 ——

void* operator new(std::size_t n) {
  ++g_allocations;
  if (void* p = std::malloc(n == 0U ? 1U : n)) {
    return p;
  }
  throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
  std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}

void* operator new[](std::size_t n) {
  ++g_allocations;
  if (void* p = std::malloc(n == 0U ? 1U : n)) {
    return p;
  }
  throw std::bad_alloc();
}

void operator delete[](void* p) noexcept {
  std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
  std::free(p);
}

// —— C++17 对齐分配 ——

void* operator new(std::size_t n, std::align_val_t alignment) {
  ++g_allocations;
  // _aligned_malloc 自己处理对齐（不要求 size 是对齐的倍数），失败返回 nullptr。
  if (void* p = _aligned_malloc(n == 0U ? 1U : n, static_cast<std::size_t>(alignment))) {
    return p;
  }
  throw std::bad_alloc();
}

void operator delete(void* p, std::align_val_t) noexcept {
  _aligned_free(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  _aligned_free(p);
}

void* operator new[](std::size_t n, std::align_val_t alignment) {
  ++g_allocations;
  if (void* p = _aligned_malloc(n == 0U ? 1U : n, static_cast<std::size_t>(alignment))) {
    return p;
  }
  throw std::bad_alloc();
}

void operator delete[](void* p, std::align_val_t) noexcept {
  _aligned_free(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
  _aligned_free(p);
}