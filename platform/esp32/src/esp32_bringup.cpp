/**
 * @file esp32_bringup.cpp
 * @brief 真机 bring-up 自检屏实现（判据见 esp32_bringup.h 与 platform/esp32/README.md §5）
 */
#include "esp32_bringup.h"

#include <lvgl.h>

#include <embark/font_ledger.h>
#include <middleware/elog/elog.hpp>

#include "esp32_board.h"
#include "esp32_hal.h"
#include "font_alloc_scope.h"
#include "font_service.h"

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

void check_sd_font() {
  Esp32Hal& hal = Esp32Hal::instance();
  hal::Context& context = hal.context();
  if (context.filesystem == nullptr) {
    ELOG_INFO("BRINGUP SD：文件系统未挂载（板级没配卡座引脚，或没插卡），运行时字库整体跳过");
    return;
  }
  ELOG_INFO("BRINGUP SD：{}（{}）已挂载，开始运行时字库自检", hal.sd().mount_point(),
            hal.sd().info());

  // 账本与服务都是"一个进程一份"的对象：函数内静态，绕开静态初始化顺序。
  static FontLedger ledger;
  static FontService service(*context.filesystem, ledger);
  if (const Error prepared = service.init(); prepared != Error::none) {
    ELOG_WARN("BRINGUP SD：字库服务没起来（错误码 {}），跳过", static_cast<int>(prepared));
    return;
  }

  etl::expected<lv_font_t*, Error> loaded = service.load(sd_font_probe_file);
  if (!loaded) {
    ELOG_WARN("BRINGUP SD：{} 加载失败（错误码 {}）—— 卡上有这个文件吗？", sd_font_probe_file,
              static_cast<int>(loaded.error()));
    return;
  }
  lv_font_t* font = *loaded;

  // 量一次宽度：走得通说明 cmap 与字形描述都解析出来了。
  lv_point_t measured{};
  lv_txt_get_size(&measured, sd_font_probe_text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

  // 真渲染一个字形：放进 label，立刻同步刷一次屏（不等 UI 循环），再删掉 label ——
  // 删完才 release，否则屏上会留着指向已释放字库的指针。
  lv_disp_t* display = lv_disp_get_default();
  if (display != nullptr) {
    lv_obj_t* label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(label, sd_font_probe_text);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_refr_now(display);
    lv_obj_del(label);
  }

  const auto refs = ledger.refs(sd_font_probe_file);
  ELOG_INFO(
      "BRINGUP SD：字库已加载并渲染；{} 的 {} 量得 {}×{} px；记账 {} / {} 字节，引用 {} "
      "次，字库池在用 "
      "{} / {} 字节",
      sd_font_probe_file, sd_font_probe_text, static_cast<int>(measured.x),
      static_cast<int>(measured.y), static_cast<unsigned>(ledger.used_bytes()),
      static_cast<unsigned>(ledger.budget_bytes()),
      refs.has_value() ? static_cast<unsigned>(*refs) : 0U,
      static_cast<unsigned>(font_alloc_outstanding_bytes()),
      static_cast<unsigned>(font_alloc_pool_bytes()));

  const Error released = service.release(sd_font_probe_file);
  if (released != Error::none) {
    ELOG_WARN("BRINGUP SD：释放失败（错误码 {}）", static_cast<int>(released));
    return;
  }
  ELOG_INFO("BRINGUP SD：释放后记账 {} 字节、在册 {} 个、字库池在用 {} 字节（都应归零）",
            static_cast<unsigned>(ledger.used_bytes()), static_cast<unsigned>(ledger.loaded()),
            static_cast<unsigned>(font_alloc_outstanding_bytes()));
}

}  // namespace embark::platform::esp32::bringup
