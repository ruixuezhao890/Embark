/**
 * 字形缓存配额公式（ADR 0017 第 6 条）：字号不设档，配额按字号算出来。
 *
 * 为什么是公式而不是实测表：设计器对每个不同的 textFontSize 都生成一个字体槽
 * （ui.c.njk:28-65），cmake 在配置期把它们全扫进 kAidoguiFonts[] —— 加一个字号零改动。
 * 所以"哪几个字号"是构建期事实，配额不该再有一张手工维护的表跟它抢真相。
 *
 * 公式：quota(px) = max(4 KB, 8 × px²)
 *   8 × px²  —— 8 bpp、单个汉字位图 ≈ px² 字节，8 个 ≈ 一屏最大字号的并发字形数（实测 5~9，
 *               见 .scratch/font-ttf 的五条验收故事）。
 *   4 KB 下限 —— 11~22 px 按 8×px² 只有 1~4 KB，装不下十几个汉字；同时**兜住硬下限**
 *               （见下），小字号不会掉到除零线上。
 *
 * 两条硬约束（都不是性能问题）：
 *   ① 配额 ≤ px² ⇒ 崩机：lv_lru_create 拿 cache_size/(px*px) 当哈希桶数
 *      （third_party/lvgl/src/misc/lv_lru.c:83），lv_lru_hash 取 h % 桶数（:301）—— 桶数 0
 *      就是整数除零。8×px² 恒 ≥ 2×px²，这条已经不可能破。
 *   ② 配额 < 单字形位图 ⇒ 整字不画：lv_tiny_ttf.c:158-168 的 lv_mem_alloc(szb) 失败 /
 *      lv_lru_set 返回非 OK 时直接 return NULL，那个字静默变成空白，不是"慢一点"。
 *
 * 头文件刻意不 include LVGL / Embark：这是纯整数算术，能脱离 LVGL 单测
 * （tests/kernel/test_font_quota.cpp），也是桥与宿主共用同一套算法的唯一办法。
 */
#ifndef EMBARK_APP_FONT_QUOTA_H
#define EMBARK_APP_FONT_QUOTA_H

#include <cstddef>

namespace embark::demo::font_quota {

/// 一个字号要能同时装下的字形数。实测一屏并发字形 5~9 个，取 8。
inline constexpr std::size_t glyphs_per_size = 8U;

/// 配额绝对下限（字节）。
inline constexpr std::size_t floor_bytes = 4U * 1024U;

/// 单个字号的配额：max(4 KB, 8 × px²)。px <= 0（字体表项坏了）按下限给。
constexpr std::size_t quota_bytes(int px) {
  if (px <= 0) {
    return floor_bytes;
  }
  const std::size_t px2 = static_cast<std::size_t>(px) * static_cast<std::size_t>(px);
  const std::size_t want = px2 * glyphs_per_size;
  return want > floor_bytes ? want : floor_bytes;
}

/// 一个字号槽的配额（表项由调用方从生成代码的字体表抄过来）。
struct QuotaEntry {
  int px;
  std::size_t bytes;
};

/// 配额合计。表外字号（不该出现）按公式算，绝不静默漏算 —— 漏算就是"账上合规、实际超发"。
inline std::size_t total_bytes(const QuotaEntry* entries, std::size_t count) {
  std::size_t total = 0U;
  for (std::size_t i = 0; i < count; ++i) {
    total += entries[i].bytes;
  }
  return total;
}

/**
 * 按字库区可用容量收敛一组配额（就地改 entries[i].bytes）。
 *
 * 先按公式给足，超了就从**最大字号**开始砍半（一屏并发字形最少、砍掉代价最低；11/12 px 是
 * 列表正文，最后才动）。下限 floor_bytes，绝不低于它 —— 低于下限等于回到除零/整字不画。
 *
 * available_bytes == 0 表示没有独立字库区（宿主：有真实堆，作用域是空操作），照公式给、不收。
 *
 * 砍到底仍然超，本函数**只停手不报错**：报错由调用方决定（桥打 ELOG_ERROR），因为这是
 * "配置算错了"而不是"运行期故障"，不该在这里 abort。
 */
inline void fit(QuotaEntry* entries, std::size_t count, std::size_t available_bytes) {
  for (std::size_t i = 0; i < count; ++i) {
    entries[i].bytes = quota_bytes(entries[i].px);
  }
  if (available_bytes == 0U || count == 0U) {
    return;
  }
  std::size_t total = total_bytes(entries, count);
  while (total > available_bytes) {
    // 找"字号最大、且还高于下限"的那一项。
    std::size_t victim = count;
    for (std::size_t i = 0; i < count; ++i) {
      if (entries[i].bytes <= floor_bytes) {
        continue;
      }
      if (victim == count || entries[i].px > entries[victim].px) {
        victim = i;
      }
    }
    if (victim == count) {
      return;  // 全部压到下限了
    }
    const std::size_t halved = entries[victim].bytes / 2U;
    entries[victim].bytes = halved > floor_bytes ? halved : floor_bytes;
    total = total_bytes(entries, count);
  }
}

}  // namespace embark::demo::font_quota

#endif  // EMBARK_APP_FONT_QUOTA_H
