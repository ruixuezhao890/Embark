/**
 * 字形缓存配额公式的单元测试（app/common/font_quota.h，ADR 0017 第 6 条）。
 *
 * 被测头刻意不 include LVGL / Embark（纯整数算术），所以本文件跟 tests 目标一起编，
 * 与 core/tests/kernel/test_ui_screen_names.cpp 同理。
 *
 * 这里钉的四条都是**硬约束**，不是"大概这样"：
 *   ① 配额恒 ≥ 4 KB（下限）；
 *   ② 配额恒 > px² —— 破了就是 lv_lru 取模除零（真机 panic）；
 *   ③ 单调不减（大字号的配额不许比小字号小）；
 *   ④ 大字号至少装得下 8 个字形（否则整字不画，静默空白）。
 */
#include <cstddef>
#include <initializer_list>

#include <doctest/doctest.h>

#include "font_quota.h"

using embark::demo::font_quota::fit;
using embark::demo::font_quota::floor_bytes;
using embark::demo::font_quota::quota_bytes;
using embark::demo::font_quota::QuotaEntry;
using embark::demo::font_quota::total_bytes;

namespace {

/// 真实字号表（app/aidogui_ui/screen.aid 里出现的 7 个 textFontSize）。
constexpr int kRealSizes[] = {11, 12, 14, 22, 28, 48, 56};

std::size_t px_squared(int px) {
  return static_cast<std::size_t>(px) * static_cast<std::size_t>(px);
}

}  // namespace

TEST_CASE("配额公式：恒 ≥ 4 KB，且恒 > px²（除零保护）") {
  // 含退化输入：0 / 负数 / 1 px。
  const int sizes[] = {0, -5, 1, 11, 12, 14, 22, 28, 48, 56, 72, 200};
  for (const int px : sizes) {
    const std::size_t quota = quota_bytes(px);
    CHECK(quota >= floor_bytes);
    CHECK(quota > px_squared(px));
    // 哈希桶数 == quota / px²，至少要 2 个桶才谈得上"缓存"。
    CHECK(quota / (px_squared(px) > 0U ? px_squared(px) : 1U) >= 2U);
  }
}

TEST_CASE("配额公式：单调不减") {
  const int sizes[] = {0, 1, 11, 12, 14, 22, 28, 48, 56, 72, 200};
  std::size_t previous = 0U;
  for (const int px : sizes) {
    const std::size_t quota = quota_bytes(px);
    CHECK(quota >= previous);
    previous = quota;
  }
}

TEST_CASE("配额公式：大字号至少装得下 8 个字形") {
  // 8 × px² 就是"8 个字形"的字节数；px >= 23 之后它压过 4 KB 下限。
  for (const int px : {23, 28, 48, 56, 72, 200}) {
    CHECK(quota_bytes(px) >= px_squared(px) * embark::demo::font_quota::glyphs_per_size);
  }
  // 小字号靠下限吃饭：11~22 px 的 8 个字形不到 4 KB，取下限。
  CHECK(quota_bytes(11) == floor_bytes);
  CHECK(quota_bytes(22) == floor_bytes);
  CHECK(quota_bytes(28) == px_squared(28) * 8U);
}

TEST_CASE("真实 7 档：总配额远低于 144 KB 可用池") {
  QuotaEntry entries[7] = {};
  for (int i = 0; i < 7; ++i) {
    entries[i].px = kRealSizes[i];
    entries[i].bytes = quota_bytes(kRealSizes[i]);
  }
  const std::size_t total = total_bytes(entries, 7U);
  // 4 × 4 KB（11/12/14/22）+ 28²×8 + 48²×8 + 56²×8 = 66,176 字节 ≈ 64.6 KB。
  CHECK(total == 66176U);
  // 字库区 192 KB × 3/4 = 147,456 字节可用。
  CHECK(total <= 147456U);
  // 收敛不该动它。
  fit(entries, 7U, 147456U);
  CHECK(total_bytes(entries, 7U) == total);
}

TEST_CASE("可用容量为 0（宿主 / 无独立字库区）：照公式给，不收") {
  QuotaEntry entries[3] = {};
  entries[0].px = 14;
  entries[1].px = 28;
  entries[2].px = 56;
  fit(entries, 3U, 0U);
  for (const QuotaEntry& entry : entries) {
    CHECK(entry.bytes == quota_bytes(entry.px));
  }
}

TEST_CASE("容量不够时从最大字号开始砍半，且绝不低于 4 KB 下限") {
  QuotaEntry entries[3] = {};
  entries[0].px = 14;
  entries[1].px = 28;
  entries[2].px = 56;
  // 预算只够 14 px 那一项 + 一点点。
  const std::size_t available = floor_bytes * 2U;
  fit(entries, 3U, available);
  // 56 px 先被砍，然后是 28 px；14 px 本来就在下限，不动。
  CHECK(entries[0].bytes == floor_bytes);
  CHECK(entries[1].bytes == floor_bytes);
  CHECK(entries[2].bytes == floor_bytes);
  CHECK(total_bytes(entries, 3U) == floor_bytes * 3U);
  // 已经全部到底：预算还是不够，也只停手，不再往下降。
  CHECK(total_bytes(entries, 3U) > available);
  // 字号本身不被改写。
  CHECK(entries[2].px == 56);
}

TEST_CASE("容量够一半：只有最大字号被砍，小字号保持公式值") {
  QuotaEntry entries[3] = {};
  entries[0].px = 14;
  entries[1].px = 28;
  entries[2].px = 56;
  const std::size_t small = quota_bytes(14);
  const std::size_t mid = quota_bytes(28);
  const std::size_t big_before = quota_bytes(56);
  // 让 56 px 砍一次（砍到 12544）刚好塞得下。
  const std::size_t available = small + mid + big_before / 2U;
  fit(entries, 3U, available);
  CHECK(entries[0].bytes == small);
  CHECK(entries[1].bytes == mid);
  CHECK(entries[2].bytes == big_before / 2U);
  CHECK(total_bytes(entries, 3U) <= available);
}

TEST_CASE("字号表为空：不崩、不写内存") {
  fit(nullptr, 0U, 147456U);
  CHECK(total_bytes(nullptr, 0U) == 0U);
}
