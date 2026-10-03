#include "host_system.h"

#include <cstdio>
#include <cstdlib>

namespace embark::platform::host {
namespace {

/// 平台级致命处理的唯一实现：写 stderr → abort。
/// 不走日志框架是刻意的 —— 这是"最后一条日志"，日志后端本身很可能就是坏掉的那个。
[[noreturn]] void die(const char* reason) noexcept {
  std::fprintf(stderr, "\n[embark] fatal: %s\n", reason != nullptr ? reason : "(null)");
  std::fflush(stderr);
  std::abort();
}

}  // namespace

void HostSystem::restart() noexcept {
  std::fprintf(stderr, "\n[embark] restart requested (host: 进程退出，退出码 70)\n");
  std::fflush(stderr);
  std::exit(70);
}

std::size_t HostSystem::free_heap_bytes() const noexcept {
  return 0;  // 宿主没有 FreeRTOS 堆；0 = 这个后端量不了
}

std::size_t HostSystem::min_free_heap_bytes() const noexcept {
  return 0;
}

std::size_t HostSystem::stack_high_water_bytes() const noexcept {
  return 0;
}

void HostSystem::feed_watchdog() noexcept {
  // 宿主没有看门狗
}

void HostSystem::fatal(const char* reason) noexcept {
  die(reason);
}

}  // namespace embark::platform::host

// --- 平台注入给内核的两个钩子（spec §9）---------------------------------------

namespace embark {

void assert_failed(const char* file, int line, const char* expression) noexcept {
  std::fprintf(stderr, "\n[embark] assertion failed: %s\n          at %s:%d\n",
               expression != nullptr ? expression : "(null)", file != nullptr ? file : "(null)",
               line);
  std::fflush(stderr);
  std::abort();
}

void fatal(const char* reason) noexcept {
  // 与 ISystem::fatal 同一个出口：平台级致命处理只有一处实现。
  platform::host::HostSystem{}.fatal(reason);
  std::abort();  // HostSystem::fatal 不返回；这行只为让 [[noreturn]] 有名有实
}

}  // namespace embark
