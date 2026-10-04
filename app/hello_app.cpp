/**
 * Embark 最简 App 实现（issues/12）：见 hello_app.h 头注释。
 *
 * 生命周期纪律与 demo_apps.cpp 完全一致（spec §5 / issues/06）：
 *   - onCreate 建自己的屏幕（lv_obj_create(nullptr) = 游离屏幕对象）；
 *   - onEnter/onResume 里 lv_scr_load 换上自己的屏幕（App 自决界面生命周期）；
 *   - 想离开前台时 request_switch() 请求，框架在循环边界执行切换。
 */
#include <embark/log.h>

#include "hello_app.h"

namespace embark::demo {

void HelloApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate", name());

  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x12241f), 0);

  lv_obj_t* label = lv_label_create(screen_);
  lv_label_set_text(label, "Hello, Embark");
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void HelloApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
  lv_scr_load(screen_);
}

void HelloApp::onPause() {
  ELOG_INFO("App {} 离开前台", name());
}

void HelloApp::onResume() {
  ELOG_INFO("App {} 回到前台", name());
  lv_scr_load(screen_);
}

void HelloApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

}  // namespace embark::demo