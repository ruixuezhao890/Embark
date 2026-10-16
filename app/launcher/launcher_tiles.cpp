/**
 * Embark · 启动器磁贴构建实现（issue launcher-pages）：见 launcher_tiles.h 头注释。
 *
 * 与设计稿的对应关系（AiDoGUI 工程 app/aidogui_ui/screen.aid 的 launcher 屏）：
 *   - 磁贴带 Band_Apps：0,202 240×56，横向可滚 + 关滚动条 + X 轴吸附 START；
 *   - 页码点容器 Dots_Row：0,258 240×6，点由这里按页数生成（设计器里的 Dot1-3 已删）；
 *   - 每页 = 一个 240×56 的透明页容器（24×48 磁贴的 4 个格子，见 launcher_pages.h）。
 * 屏上的磁贴样式（底 #202B47 / 圆角 10 / 字 #C7D0E6 14px）走 design_tokens 的
 * tile_bg / tile_radius / tile_text 与 page_dot / page_dot_active 令牌。
 *
 * 滚动方向口径：LVGL 8 的 lv_obj_get_scroll_x() 对"内容右移"给负值，这里取绝对值
 * 换算页号（页宽 = 视口宽，一格吸附 = 一页）。
 */
#include "launcher_tiles.h"

#include <cstring>

#include <embark/design_tokens.h>
#include <embark/log.h>

#include "aidogui_ui_bridge.h"
#include "aidogui_ui_nav.h"  // 磁贴点击直接走导航请求（磁贴是运行时对象，进不了绑定表）
#include "launcher_pages.h"

namespace embark::demo {
namespace {

using namespace embark::launcher;

constexpr const char* kLauncherScreen = "launcher";
constexpr const char* kBandWidget = "Band_Apps";
constexpr const char* kDotsWidget = "Dots_Row";
constexpr int kTileTextSize = 14;      // 与设计稿磁贴字同号（字号表里的 14px）
constexpr lv_coord_t kTileTextY = 15;  // 设计稿：label 48×20 @ y=15

struct TileSlot {
  const char* app = nullptr;
  lv_obj_t* obj = nullptr;
  lv_obj_t* label = nullptr;
};

TileSlot g_tiles[max_tiles] = {};
lv_obj_t* g_pages[max_pages] = {};
lv_obj_t* g_dots[max_pages] = {};
std::size_t g_tile_count = 0;
std::size_t g_page_count = 0;
std::size_t g_dot_count = 0;
std::size_t g_current_page = 0;
lv_obj_t* g_band = nullptr;

lv_obj_t* box(lv_obj_t* parent, std::size_t x, std::size_t y, std::size_t w, std::size_t h) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_size(obj, static_cast<lv_coord_t>(w), static_cast<lv_coord_t>(h));
  lv_obj_set_pos(obj, static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y));
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  return obj;
}

/// 磁贴带停在第几页：按 scroll_x 就近取整（拖到一半也会算最近的页）。
std::size_t page_from_scroll() {
  if (g_band == nullptr || g_page_count == 0U) {
    return 0U;
  }
  lv_coord_t offset = lv_obj_get_scroll_x(g_band);
  if (offset < 0) {
    offset = static_cast<lv_coord_t>(-offset);
  }
  const lv_coord_t width = static_cast<lv_coord_t>(page_width);
  std::size_t page = static_cast<std::size_t>((offset + width / 2) / width);
  if (page >= g_page_count) {
    page = g_page_count - 1U;
  }
  return page;
}

/// 把页码点涂成"当前页高亮"（翻页时重涂；页没变就不动控件，省重绘）。
void paint_dots() {
  const std::size_t page = page_from_scroll();
  if (page == g_current_page && g_dot_count == g_page_count) {
    return;
  }
  g_current_page = page;
  for (std::size_t i = 0; i < g_dot_count; ++i) {
    if (g_dots[i] == nullptr) {
      continue;
    }
    lv_obj_set_style_bg_color(
        g_dots[i],
        lv_color_hex(i == page ? design_tokens::page_dot_active : design_tokens::page_dot), 0);
  }
}

void on_band_scrolled(lv_event_t* event) {
  (void)event;
  paint_dots();
}

void on_tile_clicked(lv_event_t* event) {
  const auto* slot = static_cast<const TileSlot*>(lv_event_get_user_data(event));
  if (slot == nullptr || slot->app == nullptr) {
    return;
  }
  ELOG_INFO("主屏磁贴点击：{}", slot->app);
  (void)aidogui_ui_nav_request_app(slot->app);  // 与设计器事件同一条导航路
}

void clear_bookkeeping() {
  g_tile_count = 0;
  g_page_count = 0;
  g_dot_count = 0;
  g_current_page = 0;
  g_band = nullptr;
  for (std::size_t i = 0; i < max_tiles; ++i) {
    g_tiles[i] = TileSlot{};
  }
  for (std::size_t i = 0; i < max_pages; ++i) {
    g_pages[i] = nullptr;
    g_dots[i] = nullptr;
  }
}

}  // namespace

std::size_t launcher_tiles_rebuild(const char* screen_name, const LauncherTile* tiles,
                                   std::size_t count) {
  if (screen_name == nullptr || tiles == nullptr ||
      std::strcmp(screen_name, kLauncherScreen) != 0) {
    return 0U;
  }
  lv_obj_t* band = aidogui_ui_bridge_widget(kLauncherScreen, kBandWidget);
  lv_obj_t* dots_row = aidogui_ui_bridge_widget(kLauncherScreen, kDotsWidget);
  if (band == nullptr || dots_row == nullptr) {
    ELOG_WARN("主屏磁贴：屏 {} 里没有 {} / {}（设计器里改名了？），不重建", kLauncherScreen,
              kBandWidget, kDotsWidget);
    clear_bookkeeping();
    return 0U;
  }

  // 屏重建 = 控件是新的（旧对象随屏 destroy 一起没了），先清空两个容器的子件。
  lv_obj_clean(band);
  lv_obj_clean(dots_row);
  clear_bookkeeping();
  g_band = band;

  // 磁贴带的滚动参数在设计器里也画了（screen.aid 的 Band_Apps）；这里再写一遍是防漂移：
  // 磁贴是运行时建的，设计器那边改错了没有别的兜底。
  lv_obj_add_flag(band, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(band, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(band, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_snap_x(band, LV_SCROLL_SNAP_START);
  lv_obj_set_scroll_snap_y(band, LV_SCROLL_SNAP_NONE);

  const std::size_t tiles_total = count > max_tiles ? max_tiles : count;
  g_page_count = page_count(tiles_total);
  g_dot_count = g_page_count;

  for (std::size_t page = 0; page < g_page_count; ++page) {
    g_pages[page] = box(band, page_x(page), 0, page_width, page_height);
  }

  for (std::size_t index = 0; index < tiles_total; ++index) {
    const std::size_t page = page_of(index);
    const std::size_t slot = slot_of(index);
    lv_obj_t* tile = box(g_pages[page], tile_x(slot), 0, tile_size, tile_size);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(design_tokens::tile_bg), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, design_tokens::tile_radius, 0);
    g_tiles[index].app = tiles[index].app_name;
    g_tiles[index].obj = tile;
    lv_obj_add_event_cb(tile, &on_tile_clicked, LV_EVENT_CLICKED, &g_tiles[index]);

    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, tiles[index].label != nullptr ? tiles[index].label : "");
    lv_obj_set_width(label, static_cast<lv_coord_t>(tile_size));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(design_tokens::tile_text), 0);
    if (lv_font_t* font = aidogui_ui_bridge_font(kTileTextSize)) {
      lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, kTileTextY);
    g_tiles[index].label = label;
  }
  g_tile_count = tiles_total;

  const std::size_t active = g_current_page;  // 先记下：paint_dots 会重算
  for (std::size_t page = 0; page < g_page_count; ++page) {
    lv_obj_t* dot = box(dots_row, dot_x(page, g_page_count), 0, dot_size, dot_size);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(dot, static_cast<lv_coord_t>(dot_size / 2U), 0);
    lv_obj_set_style_bg_color(
        dot,
        lv_color_hex(page == active ? design_tokens::page_dot_active : design_tokens::page_dot), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    g_dots[page] = dot;
  }

  lv_obj_add_event_cb(band, &on_band_scrolled, LV_EVENT_SCROLL, nullptr);
  lv_obj_scroll_to(band, 0, 0, LV_ANIM_OFF);  // 新屏从第 1 页开始（重建也幂等）

  ELOG_INFO("主屏磁贴：建出 {} 个磁贴 / {} 页 / {} 个页码点", g_tile_count, g_page_count,
            g_dot_count);
  return g_tile_count;
}

void launcher_tiles_show_page(std::size_t page) {
  if (g_band == nullptr || g_page_count == 0U) {
    return;  // 屏没装载 / 还没建过：没有可跳的页（记日志只会刷屏，静默返回）
  }
  if (page >= g_page_count) {
    page = g_page_count - 1U;
  }
  lv_obj_scroll_to(g_band, static_cast<lv_coord_t>(page * page_width), 0, LV_ANIM_OFF);
  paint_dots();  // 页码点跟着走（paint_dots 自己会从 scroll_x 反推当前页）
  ELOG_INFO("主屏磁贴：跳到第 {} 页（共 {} 页）", page + 1U, g_page_count);
}

std::size_t launcher_tiles_count() {
  return g_tile_count;
}
std::size_t launcher_tiles_page_count() {
  return g_page_count;
}
std::size_t launcher_tiles_dot_count() {
  return g_dot_count;
}
std::size_t launcher_tiles_current_page() {
  return page_from_scroll();
}
lv_obj_t* launcher_tiles_band() {
  return g_band;
}

lv_obj_t* launcher_tiles_page(std::size_t index) {
  return index < g_page_count ? g_pages[index] : nullptr;
}
lv_obj_t* launcher_tiles_tile(std::size_t index) {
  return index < g_tile_count ? g_tiles[index].obj : nullptr;
}
lv_obj_t* launcher_tiles_tile_label(std::size_t index) {
  return index < g_tile_count ? g_tiles[index].label : nullptr;
}
lv_obj_t* launcher_tiles_dot(std::size_t index) {
  return index < g_dot_count ? g_dots[index] : nullptr;
}
const char* launcher_tiles_app(std::size_t index) {
  return index < g_tile_count ? g_tiles[index].app : nullptr;
}

}  // namespace embark::demo
