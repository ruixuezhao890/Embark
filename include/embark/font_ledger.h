/**
 * 运行时字库记账（ADR 0007 / issue 17）
 *
 * 只管账：**字节预算 + 引用计数**，零堆（定长表）、不碰 LVGL、不碰文件系统。
 * 真正的加载/释放动作在 platform/common/font_service.cpp —— 它先 acquire 拿到额度，
 * 再 lv_font_load；加载失败就把刚占的额度 release 回去，于是"账目 == 实际驻留的字库"。
 *
 * 为什么要单独一层：lv_font_load 只认字节、不问预算；LVGL 全局堆预算
 * （lvgl_alloc_budget_bytes）算的是"整个 UI 还能分配多少"，字库要单独卡一条线
 * （真机上字库缓冲将来走 PSRAM 显式分配，与 LVGL 静态池不是一笔账）。
 *
 * 容量在 config/embark_limits.h：font_budget_bytes / font_max_loaded / font_path_bytes
 * （同步 docs/reference/limits.md）。
 */
#ifndef EMBARK_FONT_LEDGER_H
#define EMBARK_FONT_LEDGER_H

#include <cstddef>

#include <embark/error.h>
#include <embark_limits.h>
#include <middleware/etl/expected.h>
#include <middleware/etl/string_view.h>

namespace embark {

class FontLedger {
 public:
  /// 记一笔"我要加载这个路径"，bytes = 字库文件字节数（只有首次记费）。
  /// 命中已有条目 → 引用 +1（不重复计费），返回新的引用数；未命中 → 预算与表项都够才新增，返回 1。
  /// 错误：路径空/超长或 bytes 为 0 → invalid_argument；预算不够、表项用完 → no_space。
  [[nodiscard]] etl::expected<std::size_t, Error> acquire(etl::string_view path,
                                                          std::size_t bytes) noexcept;

  /// 引用 -1；归零时清掉条目并退回预算。账上没有这个路径 → not_found。
  [[nodiscard]] Error release(etl::string_view path) noexcept;

  /// 查引用数（不改账）；账上没有这个路径 → not_found。
  [[nodiscard]] etl::expected<std::size_t, Error> refs(etl::string_view path) const noexcept;

  [[nodiscard]] std::size_t loaded() const noexcept { return count_; }
  [[nodiscard]] std::size_t used_bytes() const noexcept { return used_bytes_; }
  [[nodiscard]] static constexpr std::size_t budget_bytes() noexcept { return font_budget_bytes; }

 private:
  struct Entry {
    char path[font_path_bytes] = {};
    std::size_t bytes = 0;
    std::size_t refs = 0;
  };

  /// 条目下标；没有这个路径（或路径本身非法）→ font_max_loaded（"没有"的哨兵）。
  [[nodiscard]] std::size_t find(etl::string_view path) const noexcept;

  Entry entries_[font_max_loaded]{};
  std::size_t count_ = 0;  ///< 前 count_ 个条目非空（release 用尾部补位，不留空洞）
  std::size_t used_bytes_ = 0;  ///< 已记费字节数
};

}  // namespace embark

#endif /* EMBARK_FONT_LEDGER_H */
