/**
 * Embark · 启动器磁贴的分页与几何（纯函数，issue launcher-pages）
 *
 * 主屏的 App 磁贴由 App 注册表**运行时生成**（issue launcher-pages 决策 Q2）：注册表里
 * 第 0 项是主屏自己，不给自己磁贴，所以磁贴数 = App 数 − 1。
 * 磁贴按 4 个一页铺开，横向分页滑动（吸附式连续滚动，见 launcher_tiles.cpp）。
 *
 * 这里【只有算术】：不 include LVGL、不认识 App、不碰全局状态 —— 页面数与几何坐标
 * 都能在宿主单测里直接算（tests/kernel/test_launcher_pages.cpp）。谁负责建控件、
 * 谁负责布局，是 launcher_tiles.cpp 的事。
 *
 * 坐标口径：页内局部坐标（页容器左上角为原点），横向分页的页宽 = 面板宽。
 */
#ifndef EMBARK_APP_LAUNCHER_PAGES_H
#define EMBARK_APP_LAUNCHER_PAGES_H

#include <cstddef>

#include <embark_limits.h>  // max_apps：磁贴容量与注册表容量只在一处定义

namespace embark::launcher {

/// 每页磁贴数（2×2 网格：两行两列，列距由 tile_x 给出）。
inline constexpr std::size_t tiles_per_page = 4U;

/// 磁贴边长（px）。
inline constexpr std::size_t tile_size = 48U;

/// 磁贴带的纵向位置（屏坐标；与设计稿一致，磁贴带贴在主卡片下方）。
inline constexpr std::size_t tile_y = 202U;

/// 磁贴带高度（可视高度 = 一页的高度；磁贴 48 高，上下各留 4 让滚动视觉不贴边）。
inline constexpr std::size_t page_height = 56U;

/// 面板宽度 = 页宽（一页正好一个视口宽 → 一格吸附 = 一页）。
inline constexpr std::size_t page_width = 240U;

/// 页内第一列磁贴的 x，以及列距（设计稿：12 / 68 / 124 / 180）。
inline constexpr std::size_t tile_x0 = 12U;
inline constexpr std::size_t tile_dx = 56U;

/// 页码点：6×6、间距 2，整行在屏宽中点居中。
inline constexpr std::size_t dot_size = 6U;
inline constexpr std::size_t dot_gap = 2U;

/// 磁贴容量：注册表容量减一（第 0 项是主屏自己，不给自己磁贴）。取值来自
/// config/embark_limits.h 的 max_apps —— 改容量只改那一处，这里跟着走。
inline constexpr std::size_t max_tiles = embark::max_apps - 1U;

/// 页容量 = 磁贴满编时的页数（运行时对象数组按它定长）。
inline constexpr std::size_t max_pages = (max_tiles + tiles_per_page - 1U) / tiles_per_page;

/// 磁贴数：注册表第 0 项（主屏自己）不占磁贴。
constexpr std::size_t tile_count(std::size_t app_count) noexcept {
  return app_count == 0U ? 0U : app_count - 1U;
}

/// 页数：每页 tiles_per_page 个，向上取整；**至少 1 页**（0 个磁贴也要有承载容器，
/// 否则屏上会出现"带子不见了"的空洞，界面上无法与"没建出来"区分）。
constexpr std::size_t page_count(std::size_t tiles) noexcept {
  return tiles == 0U ? 1U : (tiles + tiles_per_page - 1U) / tiles_per_page;
}

/// 第 tile_index 个磁贴落在第几页 / 页内第几格。
constexpr std::size_t page_of(std::size_t tile_index) noexcept { return tile_index / tiles_per_page; }
constexpr std::size_t slot_of(std::size_t tile_index) noexcept { return tile_index % tiles_per_page; }

/// 页内第 slot 格的 x；第 page 页在磁贴带里的 x。
constexpr std::size_t tile_x(std::size_t slot) noexcept { return tile_x0 + slot * tile_dx; }
constexpr std::size_t page_x(std::size_t page) noexcept { return page * page_width; }

/// 页码点整行宽度（n 个点 + n−1 个间距）。
constexpr std::size_t dots_row_width(std::size_t pages) noexcept {
  return pages == 0U ? 0U : pages * dot_size + (pages - 1U) * dot_gap;
}

/// 整行居中后的行起点 x（在屏坐标里）。
constexpr std::size_t dots_row_x(std::size_t pages) noexcept {
  return page_width / 2U - dots_row_width(pages) / 2U;
}

/// 第 page 个点的 x（整行已居中）。
constexpr std::size_t dot_x(std::size_t page, std::size_t pages) noexcept {
  return dots_row_x(pages) + page * (dot_size + dot_gap);
}

}  // namespace embark::launcher

#endif /* EMBARK_APP_LAUNCHER_PAGES_H */
