// 测试侧对 embark::assert_failed / embark::fatal 的实现。
//
// 真值来自 platform/host/host_system.cpp（写 stderr + abort）；测试这边做同一件事，
// 外加把最后一次失败文本抄下来，方便排查。测试 target 故意不链接平台库，所以这两个
// 符号在这里提供，不会重复定义。
#include "fakes/fakes.h"

#include <embark/diagnostics.h>

#include <cstdio>
#include <cstdlib>

namespace embark::fakes {
namespace {

std::size_t g_assert_failed_calls = 0;
std::size_t g_fatal_calls = 0;
etl::array<char, 128> g_last_failure{};
std::size_t g_last_failure_length = 0;

void record(const char* text) noexcept {
  std::size_t i = 0;
  if (text != nullptr) {
    for (; i + 1 < g_last_failure.size() && text[i] != '\0'; ++i) {
      g_last_failure[i] = text[i];
    }
  }
  g_last_failure_length = i;
  g_last_failure[i] = '\0';
}

}  // namespace

std::size_t assert_failed_calls() noexcept {
  return g_assert_failed_calls;
}

std::size_t fatal_calls() noexcept {
  return g_fatal_calls;
}

etl::string_view last_failure_text() noexcept {
  return etl::string_view(g_last_failure.data(), g_last_failure_length);
}

void reset_diagnostics() noexcept {
  g_assert_failed_calls = 0;
  g_fatal_calls = 0;
  g_last_failure_length = 0;
  g_last_failure[0] = '\0';
}

}  // namespace embark::fakes

namespace embark {

void assert_failed(const char* file, int line, const char* expression) noexcept {
  ++fakes::g_assert_failed_calls;
  std::fprintf(stderr, "\n[embark-test] assertion failed: %s\n               at %s:%d\n",
               expression != nullptr ? expression : "(null)", file != nullptr ? file : "(null)",
               line);
  fakes::record(expression != nullptr ? expression : "(null)");
  std::fflush(stderr);
  std::abort();
}

void fatal(const char* reason) noexcept {
  ++fakes::g_fatal_calls;
  std::fprintf(stderr, "\n[embark-test] fatal: %s\n", reason != nullptr ? reason : "(null)");
  fakes::record(reason != nullptr ? reason : "(null)");
  std::fflush(stderr);
  std::abort();
}

}  // namespace embark
