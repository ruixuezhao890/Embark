// 启动器磁贴的分页与几何（issue launcher-pages）：纯函数，脱离 LVGL 单测。
#include <doctest/doctest.h>

#include <cstddef>

#include "launcher/launcher_pages.h"

using embark::launcher::dot_gap;
using embark::launcher::dot_size;
using embark::launcher::dot_x;
using embark::launcher::dots_row_width;
using embark::launcher::dots_row_x;
using embark::launcher::page_count;
using embark::launcher::page_of;
using embark::launcher::page_width;
using embark::launcher::page_x;
using embark::launcher::slot_of;
using embark::launcher::tile_count;
using embark::launcher::tile_size;
using embark::launcher::tile_x;
using embark::launcher::tile_y;
using embark::launcher::tiles_per_page;

TEST_CASE("磁贴数 = App 数 − 1（注册表第 0 项是主屏自己）") {
  CHECK(tile_count(0U) == 0U);
  CHECK(tile_count(1U) == 0U);  // 只有主屏：一个磁贴都没有
  CHECK(tile_count(2U) == 1U);
  CHECK(tile_count(3U) == 2U);
  CHECK(tile_count(4U) == 3U);
  CHECK(tile_count(5U) == 4U);
  CHECK(tile_count(6U) == 5U);  // launcher + 时钟/设置/天气/音乐/相册
  CHECK(tile_count(11U) == 10U);
  CHECK(tile_count(12U) == 11U);
}

TEST_CASE("页数：每页 4 格向上取整，0 个磁贴也留一页") {
  CHECK(page_count(0U) == 1U);
  CHECK(page_count(1U) == 1U);
  CHECK(page_count(4U) == 1U);
  CHECK(page_count(5U) == 2U);
  CHECK(page_count(8U) == 2U);
  CHECK(page_count(9U) == 3U);
  CHECK(page_count(11U) == 3U);
  CHECK(page_count(12U) == 3U);
}

TEST_CASE("App 数 → 页数（3/4/5/6/11/12 个 App 的表）") {
  CHECK(page_count(tile_count(3U)) == 1U);   //  2 磁贴
  CHECK(page_count(tile_count(4U)) == 1U);   //  3
  CHECK(page_count(tile_count(5U)) == 1U);   //  4（正好一页）
  CHECK(page_count(tile_count(6U)) == 2U);   //  5（多出 1 个 → 第二页）
  CHECK(page_count(tile_count(7U)) == 2U);   //  6
  CHECK(page_count(tile_count(11U)) == 3U);  // 10
  CHECK(page_count(tile_count(12U)) == 3U);  // 11（容量上限 max_apps=12：3 页）
}

TEST_CASE("每个磁贴都落在有效页与有效格内（全展开）") {
  for (std::size_t app_count = 0U; app_count <= 12U; ++app_count) {
    const std::size_t tiles = tile_count(app_count);
    const std::size_t pages = page_count(tiles);
    CHECK(pages >= 1U);
    CHECK(tiles <= pages * tiles_per_page);
    for (std::size_t i = 0U; i < tiles; ++i) {
      CHECK(page_of(i) < pages);
      CHECK(slot_of(i) < tiles_per_page);
      // 序号 ↔ (页, 格) 是双射：不会漏磁贴，也不会两个磁贴叠在同一格。
      CHECK(page_of(i) * tiles_per_page + slot_of(i) == i);
      // 磁贴不越出页宽（横向不裁切）。
      CHECK(tile_x(slot_of(i)) + tile_size <= page_width);
    }
  }
}

TEST_CASE("磁贴几何 = 设计稿（x = 12/68/124/180，y = 202）") {
  CHECK(tile_x(0U) == 12U);
  CHECK(tile_x(1U) == 68U);
  CHECK(tile_x(2U) == 124U);
  CHECK(tile_x(3U) == 180U);
  // 左右边距对称：12 与 240 − (180 + 48) = 12。
  CHECK(tile_x(3U) + tile_size + 12U == page_width);
  CHECK(tile_y == 202U);
  CHECK(tile_size == 48U);
}

TEST_CASE("页在磁贴带里的 x：一格 = 一页宽") {
  CHECK(page_x(0U) == 0U);
  CHECK(page_x(1U) == 240U);
  CHECK(page_x(2U) == 480U);
}

TEST_CASE("页码点整行居中（设计稿 2 个点 = 113/121，3 个点 = 109/117/125）") {
  CHECK(dots_row_width(1U) == dot_size);
  CHECK(dots_row_width(2U) == 2U * dot_size + dot_gap);
  CHECK(dots_row_x(2U) == 113U);
  CHECK(dot_x(0U, 2U) == 113U);
  CHECK(dot_x(1U, 2U) == 121U);
  CHECK(dots_row_x(3U) == 109U);
  CHECK(dot_x(0U, 3U) == 109U);
  CHECK(dot_x(1U, 3U) == 117U);
  CHECK(dot_x(2U, 3U) == 125U);
  // 奇数个点时中点对齐；偶数个点时中点落在两个点之间（+0.5px，向上取整）。
  CHECK(dots_row_x(1U) == 117U);
  CHECK(dots_row_width(0U) == 0U);
}
