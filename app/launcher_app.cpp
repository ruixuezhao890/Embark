/**
 * Embark · 启动器 App 实现（issue 16、ADR 0006）
 *
 * 实现口径见 launcher_app.h 头注释；本文件只做三件事：
 *   1. 建屏（弧轨 / 选中弧段 / 槽圆 / 图标 / 标题）；
 *   2. 收输入（按下/拖动/抬起/按键），把手指位移换算成扇面位移；
 *   3. 弹簧（5 ms LVGL 定时器）把扇面吸到最近的整槽。
 *
 * 一个重要的输入约定：全屏承接输入，命中测试是手算的（hit_slot），所以
 * 所有子对象（弧线、槽圆、图标、标题）都显式清掉 CLICKABLE + SCROLLABLE，
 * LVGL 的命中测试会跳过它们，按下事件一定落在 screen_ 上。
 */
#include <cmath>
#include <cstring>

#include <embark/design_tokens.h>
#include <embark/error.h>
#include <embark/log.h>

#include "launcher_app.h"

LV_FONT_DECLARE(embark_zh_14);

namespace embark::demo {
namespace {

constexpr float kPi = 3.14159265358979323846F;

/// 浮点坐标取整为 LVGL 坐标（四舍五入）。
lv_coord_t coord(float value) noexcept { return static_cast<lv_coord_t>(std::lround(value)); }

}  // namespace

void LauncherApp::onCreate(Framework& fw) {
  fw_ = &fw;
  slot_count_ = static_cast<int>(fw.apps().size());
  ELOG_INFO("App {} onCreate：{} 个槽（注册表 App 数）", name(), slot_count_);

  // 全屏承接输入：子对象全部不可点击，按下一定落在屏上。
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_size(screen_, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  lv_obj_set_style_bg_color(screen_, lv_color_hex(design_tokens::bg), 0);
  lv_obj_add_flag(screen_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  // 键盘/滚轮路径：把屏加进默认组（宿主还没有键盘 indev，真机接上即生效）。
  if (lv_group_get_default() == nullptr) {
    lv_group_set_default(lv_group_create());
  }
  lv_group_add_obj(lv_group_get_default(), screen_);

  lv_obj_add_event_cb(screen_, &LauncherApp::on_screen_event, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(screen_, &LauncherApp::on_screen_event, LV_EVENT_PRESSING, this);
  lv_obj_add_event_cb(screen_, &LauncherApp::on_screen_event, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(screen_, &LauncherApp::on_screen_event, LV_EVENT_KEY, this);

  build_panel();

  // 弹簧：5 ms 一推（与 UI 节拍 ui_loop_period_ms 同频）；前台才干活（pause/resume）。
  spring_timer_ = lv_timer_create(&LauncherApp::on_spring_timer, 5, this);
}

void LauncherApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
  rebuild_panel();
  if (spring_timer_ != nullptr) {
    lv_timer_resume(spring_timer_);
  }
}

void LauncherApp::onPause() {
  ELOG_INFO("App {} 退到后台（enter {}, resume {}）", name(), enters_, resumes_);
  if (spring_timer_ != nullptr) {
    lv_timer_pause(spring_timer_);
  }
}

void LauncherApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
  lv_scr_load(screen_);
  rebuild_panel();
  if (spring_timer_ != nullptr) {
    lv_timer_resume(spring_timer_);
  }
}

void LauncherApp::onExit() {
  if (spring_timer_ != nullptr) {
    lv_timer_del(spring_timer_);
    spring_timer_ = nullptr;
  }
}

void LauncherApp::apply_target(float new_target) noexcept {
  target_ = embark::launcher::clamp_target(new_target, slot_count_);
  if (!embark::launcher::spring_settled(pos_, target_)) {
    animating_ = true;
  }
}

void LauncherApp::settle_at(float snapped_target) noexcept {
  apply_target(embark::launcher::clamp_target(snapped_target, slot_count_));
}

int LauncherApp::hit_slot(lv_coord_t x, lv_coord_t y) const noexcept {
  const float dx = static_cast<float>(x) - embark::launcher::pivot_x;
  const float dy = static_cast<float>(y) - embark::launcher::pivot_y;
  const float dist = std::sqrt(dx * dx + dy * dy);
  if (std::fabs(dist - embark::launcher::radius) > static_cast<float>(slot_hit_radius)) {
    return -1;  // 不在槽的环形命中带里
  }
  // 角度口径与 launcher_geometry 一致：0° = 正上方，顺时针为正。
  const float degrees = std::atan2(dx, -dy) * 180.0F / kPi;
  const float slot_float = degrees / embark::launcher::step_deg + pos_;
  int slot = embark::launcher::normalize_index(
      static_cast<int>(std::lround(slot_float)), slot_count_);
  // 严格校验：这个槽在当前扇面位置下真的该在这个角度（命中带内的槽可能相邻）。
  const float offset = embark::launcher::slot_offset(slot, pos_);
  if (std::fabs(offset - degrees) > embark::launcher::step_deg * 0.5F) {
    return -1;
  }
  return slot;
}

void LauncherApp::build_panel() noexcept {
  // --- 虚线弧轨（整条半圆；选中弧段画在它上面）-------------------------------
  rail_ = lv_line_create(screen_);
  for (int i = 0; i < rail_point_count; ++i) {
    const float degrees = -90.0F + 6.0F * static_cast<float>(i);
    const embark::launcher::Point point = embark::launcher::slot_center(degrees);
    rail_points_[i].x = coord(point.x);
    rail_points_[i].y = coord(point.y);
  }
  lv_line_set_points(rail_, rail_points_, rail_point_count);
  lv_obj_set_style_line_color(rail_, lv_color_hex(design_tokens::rail), 0);
  lv_obj_set_style_line_width(rail_, 2, 0);
  lv_obj_set_style_line_dash_width(rail_, 2, 0);
  lv_obj_set_style_line_dash_gap(rail_, 6, 0);
  lv_obj_set_style_line_rounded(rail_, 1, 0);
  lv_obj_clear_flag(rail_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // --- 实线选中弧段（选中槽永远停在正上方，弧段就在 0° 附近）------------------
  highlight_ = lv_line_create(screen_);
  for (int i = 0; i < highlight_point_count; ++i) {
    const float degrees = -13.0F + (26.0F / static_cast<float>(highlight_point_count - 1)) *
                                      static_cast<float>(i);
    const embark::launcher::Point point = embark::launcher::slot_center(degrees);
    highlight_points_[i].x = coord(point.x);
    highlight_points_[i].y = coord(point.y);
  }
  lv_line_set_points(highlight_, highlight_points_, highlight_point_count);
  lv_obj_set_style_line_color(highlight_, lv_color_hex(design_tokens::accent), 0);
  lv_obj_set_style_line_width(highlight_, 2, 0);
  lv_obj_set_style_line_rounded(highlight_, 1, 0);
  lv_obj_clear_flag(highlight_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // --- 槽：圆 + 图标 + 标题 ---------------------------------------------------
  static_assert(embark::launcher::max_slots >= 5U,
                "启动器要能装下 4 个 demo App + 自己（issue 16 契约）");
  for (int i = 0; i < slot_count_; ++i) {
    App* const app = fw_->apps().at(i);

    lv_obj_t* const root = lv_obj_create(screen_);
    lv_obj_set_size(root, slot_diameter, slot_diameter);
    lv_obj_set_style_radius(root, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    slot_roots_[i] = root;

    // 图标：App::icon() 的 LV_SYMBOL 码点；没有则取标题首字符兜底（UTF-8）。
    const char* const app_icon = app->icon();
    const char* icon_text = app_icon;
    if (icon_text == nullptr || icon_text[0] == '\0') {
      char fallback[8];
      fallback[0] = '\0';
      if (embark::launcher::first_utf8_char(app->title(), fallback, sizeof(fallback))) {
        std::strncpy(slot_icon_text_[i], fallback, sizeof(slot_icon_text_[i]) - 1);
        slot_icon_text_[i][sizeof(slot_icon_text_[i]) - 1] = '\0';
        icon_text = slot_icon_text_[i];
      }
    }
    lv_obj_t* const icon = lv_label_create(root);
    lv_label_set_text(icon, icon_text != nullptr ? icon_text : "");
    lv_obj_set_style_text_font(icon, &embark_zh_14, 0);
    lv_obj_center(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    slot_icons_[i] = icon;

    // 标题：圆下方（宽 48，居中在槽中心水平线上）。
    lv_obj_t* const title = lv_label_create(screen_);
    lv_label_set_text(title, app->title());
    lv_obj_set_width(title, 48);
    lv_obj_set_style_text_font(title, &embark_zh_14, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(design_tokens::text_secondary), 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    slot_titles_[i] = title;
  }

  rebuild_panel();
}

void LauncherApp::rebuild_panel() noexcept {
  if (screen_ == nullptr || slot_count_ <= 0) {
    return;
  }
  for (int i = 0; i < slot_count_; ++i) {
    const float offset = embark::launcher::slot_offset(i, pos_);
    const embark::launcher::Point center = embark::launcher::slot_center(offset);
    const bool is_selected = embark::launcher::is_selected(offset);

    lv_obj_t* const root = slot_roots_[i];
    lv_obj_set_pos(root, coord(center.x - slot_diameter / 2.0F),
                   coord(center.y - slot_diameter / 2.0F));
    if (is_selected) {
      lv_obj_set_style_bg_color(root, lv_color_hex(design_tokens::accent), 0);
      lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(root, 0, 0);
    } else {
      lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(root, 2, 0);
      lv_obj_set_style_border_color(root, lv_color_hex(design_tokens::ring), 0);
    }

    lv_obj_t* const icon = slot_icons_[i];
    lv_obj_set_style_text_color(
        icon, lv_color_hex(is_selected ? design_tokens::accent_contrast
                                       : design_tokens::text_primary),
        0);

    lv_obj_t* const title = slot_titles_[i];
    lv_obj_set_pos(title, coord(center.x - 24.0F), coord(center.y + 18.0F));
    lv_obj_set_style_text_color(
        title, lv_color_hex(is_selected ? design_tokens::accent
                                        : design_tokens::text_secondary),
        0);
  }
}

void LauncherApp::on_screen_event(lv_event_t* event) noexcept {
  auto* const self = static_cast<LauncherApp*>(lv_event_get_user_data(event));
  if (self == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(event);

  // 取当前指针位置（KEY 事件没有指针，跳过取点）。
  lv_point_t point{};
  const bool have_point = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
                          code == LV_EVENT_RELEASED;
  if (have_point) {
    lv_indev_t* const indev = lv_event_get_indev(event);
    if (indev != nullptr) {
      lv_indev_get_point(indev, &point);
    }
  }

  switch (code) {
    case LV_EVENT_PRESSED:
      self->dragging_ = true;
      self->animating_ = false;  // 手指按住 = 弹簧让位
      self->drag_start_pos_ = self->pos_;
      self->drag_start_y_ = point.y;
      self->tap_x_ = point.x;
      self->tap_y_ = point.y;
      break;

    case LV_EVENT_PRESSING: {
      if (!self->dragging_) {
        break;
      }
      const float dy = static_cast<float>(point.y - self->drag_start_y_);
      self->pos_ = embark::launcher::drag_target(self->drag_start_pos_, dy, self->slot_count_);
      self->rebuild_panel();
      break;
    }

    case LV_EVENT_RELEASED: {
      self->dragging_ = false;
      const float dy = static_cast<float>(point.y - self->drag_start_y_);
      const float dx = static_cast<float>(point.x - self->tap_x_);
      if (std::fabs(dy) <= static_cast<float>(self->tap_slop_px) &&
          std::fabs(dx) <= static_cast<float>(self->tap_slop_px)) {
        // 点按：命中槽才处理。
        const int slot = self->hit_slot(self->tap_x_, self->tap_y_);
        if (slot >= 0) {
          if (slot == self->selected() && self->settled()) {
            self->enter_app(slot);  // 点选中槽 = 启动
          } else {
            self->apply_target(static_cast<float>(slot));  // 点非选中槽 = 只转正
          }
        }
      } else {
        // 拖动结束：弹簧吸到最近的整槽。
        self->settle_at(static_cast<float>(std::lround(self->pos_)));
      }
      break;
    }

    case LV_EVENT_KEY: {
      const std::uint32_t key = lv_event_get_key(event);
      if (key == LV_KEY_LEFT || key == LV_KEY_UP) {
        self->apply_target(self->target_ - 1.0F);
      } else if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) {
        self->apply_target(self->target_ + 1.0F);
      }
      break;
    }

    default:
      break;
  }
}

void LauncherApp::on_spring_timer(lv_timer_t* timer) noexcept {
  auto* const self = static_cast<LauncherApp*>(timer->user_data);
  if (self == nullptr || !self->animating_) {
    return;
  }
  self->pos_ = embark::launcher::spring_step(self->pos_, self->target_);
  if (embark::launcher::spring_settled(self->pos_, self->target_)) {
    self->pos_ = self->target_;  // 原地钉住，不再抖动
    self->animating_ = false;
  }
  self->rebuild_panel();
}

void LauncherApp::enter_app(int slot) noexcept {
  ++requests_;
  const AppId slot_id = static_cast<AppId>(slot);
  const Error error = fw_->request_switch(slot_id);
  ELOG_INFO("启动器：点中槽 {}（{}）→ request_switch → {}",
            slot, slot >= 0 && slot < slot_count_ ? fw_->apps().at(slot)->name() : "?",
            error);
}

}  // namespace embark::demo
