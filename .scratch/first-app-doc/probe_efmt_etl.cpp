#include <cstdint>
#include <cstdio>

#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/array.h>
#include <middleware/etl/expected.h>
#include <middleware/etl/string_view.h>

namespace {

// A) efmt：把数字拼进固定缓冲（返回"完整长度"，snprintf 语义）
void probe_efmt() {
  char buf[32];
  const unsigned total = 65U;                     // 总毫秒折成的百分秒数之类的演示值
  const unsigned minutes = total / 6000U;
  const unsigned seconds = (total / 100U) % 60U;
  const unsigned cs = total % 100U;
  const std::size_t needed = e_fmt::format_to(buf, sizeof buf, "{:02}:{:02}.{:02}", minutes, seconds, cs);
  std::printf("[efmt] '%s' needed=%zu\n", buf, needed);
  const std::size_t size = e_fmt::formatted_size("{}:{}", 1, 2);
  std::printf("[efmt] size=%zu\n", size);
}

// B) etl：定容数组 / expected / string_view
constexpr std::size_t kMax = 4;
etl::array<std::uint32_t, kMax> g_marks{};
std::size_t g_used = 0;

bool push_mark(std::uint32_t v) {
  if (g_used >= g_marks.size()) return false;
  g_marks[g_used++] = v;
  return true;
}

etl::expected<std::uint32_t, int> to_uint(int v) {
  if (v < 0) return etl::unexpected<int>(-1);
  return etl::expected<std::uint32_t, int>(static_cast<std::uint32_t>(v));
}

void probe_etl(etl::string_view name) {
  std::printf("[etl] name=%.*s len=%zu\n", static_cast<int>(name.size()), name.data(), name.size());
  const auto ok = to_uint(42);
  const auto bad = to_uint(-3);
  std::printf("[etl] ok=%d val=%u bad=%d err=%d\n", ok.has_value() ? 1 : 0,
              ok.has_value() ? ok.value() : 0U, bad.has_value() ? 1 : 0,
              bad.has_value() ? 0 : bad.error());
  for (std::size_t i = 0; i < 6; ++i) {
    const bool added = push_mark(static_cast<std::uint32_t>(i));
    if (!added) {
      std::printf("[etl] 满了：用 %zu / %zu\n", g_used, g_marks.size());
      break;
    }
  }
}

}  // namespace

int main() {
  probe_efmt();
  probe_etl(etl::string_view("stopwatch"));
  return 0;
}
