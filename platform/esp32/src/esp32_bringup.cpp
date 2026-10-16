/**
 * @file esp32_bringup.cpp
 * @brief 真机 bring-up 自检屏实现（判据见 esp32_bringup.h 与 platform/esp32/README.md §5）
 */
#include "esp32_bringup.h"

#include <lvgl.h>

#include <middleware/elog/elog.hpp>

#include "esp32_board.h"

namespace embark::platform::esp32::bringup {
namespace {

// 只在唯一 UI 任务里访问（show / pump_touch 都由 main.cpp 的循环调用），不需要加锁。
lv_obj_t* g_coord = nullptr;
bool g_pressed_before = false;

/// 纯色块：去掉主题给的圆角、边框、阴影、滚动条，屏上只剩颜色 —— 颜色判定不能有干扰项。
lv_obj_t* flat(lv_obj_t* parent, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h,
               std::uint32_t rgb) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_hex(rgb), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

/// 文字标签：屏幕被 remove_style_all 剥成黑底，文字颜色必须显式给，否则默认色在黑底上看不见。
lv_obj_t* caption(lv_obj_t* parent, const char* text, std::uint32_t rgb) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
  return label;
}

}  // namespace

void show() {
  if (g_coord != nullptr) {
    return;
  }

  const std::int32_t width = static_cast<std::int32_t>(lcd_native_width);
  constexpr std::int32_t bar_height = 40;
  constexpr std::int32_t bar_gap = 8;

  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // 四角色标：面板原生竖屏 240×320，左上角就应该是 TL。
  lv_obj_align(caption(screen, "TL", 0xFFFFFF), LV_ALIGN_TOP_LEFT, 4, 4);
  lv_obj_align(caption(screen, "TR", 0xFFFFFF), LV_ALIGN_TOP_RIGHT, -4, 4);
  lv_obj_align(caption(screen, "BL", 0xFFFFFF), LV_ALIGN_BOTTOM_LEFT, 4, -4);
  lv_obj_align(caption(screen, "BR", 0xFFFFFF), LV_ALIGN_BOTTOM_RIGHT, -4, -4);

  // 中央坐标读数：按住屏幕就看得到「UI 实际收到」的点。
  g_coord = caption(screen, "--, --", 0x00FF00);
  lv_obj_align(g_coord, LV_ALIGN_TOP_MID, 0, 26);

  // 四条纯色带：R/G/B/W 从上到下。
  std::int32_t y = 56;
  flat(screen, 0, y, width, bar_height, 0xFF0000);
  y += bar_height + bar_gap;
  flat(screen, 0, y, width, bar_height, 0x00FF00);
  y += bar_height + bar_gap;
  flat(screen, 0, y, width, bar_height, 0x0000FF);
  y += bar_height + bar_gap;
  flat(screen, 0, y, width, bar_height, 0xFFFFFF);

  lv_scr_load(screen);
  ELOG_INFO(
      "BRINGUP 自检屏就绪：四角 TL/TR/BL/BR，四条色带 R/G/B/W；点四角核对坐标（左上应 ≈0,0，右下应 "
      "≈{}, {}）",
      width - 1, static_cast<std::int32_t>(lcd_native_height) - 1);
}

void pump_touch() {
  if (g_coord == nullptr) {
    return;
  }
  lv_indev_t* indev = lv_indev_get_next(nullptr);
  if (indev == nullptr) {
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(indev, &point);
  const bool pressed = indev->proc.state == LV_INDEV_STATE_PRESSED;

  if (pressed) {
    lv_label_set_text_fmt(g_coord, "%d, %d", static_cast<int>(point.x), static_cast<int>(point.y));
    if (!g_pressed_before) {  // 只在按下沿打日志：按住不放不刷屏
      ELOG_INFO("BRINGUP 触摸按下：{}, {}", static_cast<int>(point.x), static_cast<int>(point.y));
    }
  }
  g_pressed_before = pressed;
}

}  // namespace embark::platform::esp32::bringup
