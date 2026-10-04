/**
 * Embark ESP32-S3 · 系统控制后端实现（issues/11）
 */
#include "esp32_system.h"

#include <cstdio>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

namespace embark::platform::esp32 {
namespace {

/// 平台级致命处理的唯一实现：写 stderr → abort。
/// 不走日志框架是刻意的 —— 这是"最后一条日志"，日志后端本身很可能就是坏掉的那个。
/// IDF 的 abort() 会落到 panic handler：打印回溯、寄存器与任务列表，然后按
/// CONFIG_ESP_SYSTEM_PANIC 的配置重启或停机。
[[noreturn]] void die(const char* reason) noexcept {
  std::fprintf(stderr, "\n[embark] fatal: %s\n", reason != nullptr ? reason : "(null)");
  std::fflush(stderr);
  std::abort();
}

/// 只统计内部 SRAM（PSRAM 另有 8 MB，混在一起看不出真实压力）。
constexpr std::uint32_t heap_caps_internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

}  // namespace

Error Esp32System::init() noexcept {
  return Error::none;
}

void Esp32System::restart() noexcept {
  std::fprintf(stderr, "\n[embark] restart requested (esp_restart)\n");
  std::fflush(stderr);
  esp_restart();  // 不返回
}

std::size_t Esp32System::free_heap_bytes() const noexcept {
  return static_cast<std::size_t>(heap_caps_get_free_size(heap_caps_internal));
}

std::size_t Esp32System::min_free_heap_bytes() const noexcept {
  return static_cast<std::size_t>(heap_caps_get_minimum_free_size(heap_caps_internal));
}

std::size_t Esp32System::stack_high_water_bytes() const noexcept {
  const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
  return static_cast<std::size_t>(words) * sizeof(StackType_t);
}

void Esp32System::feed_watchdog() noexcept {
  (void)esp_task_wdt_reset();
}

void Esp32System::fatal(const char* reason) noexcept {
  die(reason);
}

}  // namespace embark::platform::esp32

// --- 平台注入给内核的两个钩子（spec §9）---------------------------------------
// 整个固件里这两个符号只在这里定义（宿主侧在 platform/host/host_system.cpp，
// 测试侧在 tests/fakes/fakes.cpp —— 链接期选定后端，运行期不查表）。

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
  platform::esp32::Esp32System{}.fatal(reason);
  std::abort();  // Esp32System::fatal 不返回；这行只为让 [[noreturn]] 有名有实
}

}  // namespace embark
