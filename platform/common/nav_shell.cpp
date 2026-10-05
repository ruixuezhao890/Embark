/**
 * 平台共用层 · 框架导航壳实现（issue 16）
 *
 * 见 nav_shell.h。这里补两条实现取舍：
 *
 *   - 时间只走 ITime::epoch_ms()（HAL 的墙钟）。取不到就画 "--:--"，不做回退到
 *     now_ms() —— 开机计时冒充墙钟比不显示更糟（用户会以为时间是对的）。
 *   - 文本只在内容变化时 lv_label_set_text：状态行每秒都被定时器看一次，但一分钟
 *     才真的变一次，多余的 set_text 会白白标脏整条状态行。
 */
#include "nav_shell.h"

#include <cstdio>
#include <cstring>

#include <embark/design_tokens.h>
#include <embark/log.h>

/// 静态子集字库 embark_zh_14 由 tools/font/gen_font.mjs 生成（assets/fonts/embark_zh_14.c），
/// 本文件内聚声明一次：LV_FONT_DECLARE 展开成 extern lv_font_t embark_zh_14。
LV_FONT_DECLARE(embark_zh_14);

namespace embark::platform {
namespace {

/// 状态行的排版（像素）。
constexpr lv_coord_t text_y = 6;      ///< (28 - 17) / 2 ≈ 5.5，取 6 让 14px 字形居中
constexpr lv_coord_t clock_x = 30;    ///< 让开左上角的返回键（4 + 22 + 4）
constexpr lv_coord_t policy_right = -8;

/// 时间刷新周期（ms）：一秒看一次，分钟变了才真的重画。
constexpr std::uint32_t clock_period_ms = 1000U;

/// v1 固定东八区（真机 RTC/时区服务未接，见文件头）。
constexpr std::uint64_t timezone_offset_ms = 8ULL * 60ULL * 60ULL * 1000ULL;

}  // namespace

Error NavShell::init(hal::Context& context) noexcept {
  if (ready_) {
    return Error::none;
  }
  if (context.display == nullptr || !context.display->is_ready()) {
    return Error::not_ready;
  }
  context_ = &context;

  lv_obj_t* const top = lv_layer_top();

  // --- 状态行：不可点击（否则吃掉下面 App 的输入）---------------------------
  bar_ = lv_obj_create(top);
  lv_obj_set_size(bar_, lv_disp_get_hor_res(nullptr), static_cast<lv_coord_t>(design_tokens::status_h));
  lv_obj_set_pos(bar_, 0, 0);
  lv_obj_clear_flag(bar_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(bar_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bar_, lv_color_hex(design_tokens::panel), 0);
  lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar_, 0, 0);
  lv_obj_set_style_radius(bar_, 0, 0);
  lv_obj_set_style_pad_all(bar_, 0, 0);

  clock_label_ = lv_label_create(bar_);
  lv_label_set_text(clock_label_, clock_text_);
  lv_obj_set_style_text_font(clock_label_, &embark_zh_14, 0);
  lv_obj_set_style_text_color(clock_label_, lv_color_hex(design_tokens::text_primary), 0);
  lv_obj_set_pos(clock_label_, clock_x, text_y);

  policy_label_ = lv_label_create(bar_);
  lv_label_set_text(policy_label_, policy_text_);
  lv_obj_set_style_text_font(policy_label_, &embark_zh_14, 0);
  lv_obj_set_style_text_color(policy_label_, lv_color_hex(design_tokens::text_secondary), 0);
  lv_obj_align(policy_label_, LV_ALIGN_TOP_RIGHT, policy_right, text_y);

  // --- 返回键：挂 top（不挂 bar_），这样它自己可点而状态行仍然不可点 ----------
  back_ = lv_obj_create(top);
  lv_obj_set_size(back_, back_size, back_size);
  lv_obj_set_pos(back_, back_x, back_y);
  lv_obj_clear_flag(back_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(back_, lv_color_hex(design_tokens::accent), 0);
  lv_obj_set_style_bg_opa(back_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(back_, 0, 0);
  lv_obj_set_style_radius(back_, 4, 0);
  lv_obj_set_style_pad_all(back_, 0, 0);

  lv_obj_t* const arrow = lv_label_create(back_);
  lv_label_set_text(arrow, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(arrow, &embark_zh_14, 0);
  lv_obj_set_style_text_color(arrow, lv_color_hex(design_tokens::accent_contrast), 0);
  lv_obj_center(arrow);

  lv_obj_add_event_cb(back_, &NavShell::on_back_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_flag(back_, LV_OBJ_FLAG_HIDDEN);  // 主屏（启动器）前台时不画返回键

  clock_timer_ = lv_timer_create(&NavShell::on_clock_timer, clock_period_ms, this);

  refresh_clock();
  refresh_policy();
  ready_ = true;
  ELOG_INFO("导航壳就绪：状态行高 {}，返回键中心 ({}, {})，时间源 {}",
            static_cast<int>(design_tokens::status_h), static_cast<int>(back_center_x),
            static_cast<int>(back_center_y), clock_timer_ != nullptr ? "1 s 定时器" : "未起");
  return Error::none;
}

void NavShell::set_foreground(AppId id) noexcept {
  foreground_ = id;
  if (back_ == nullptr) {
    return;
  }
  // 主屏（注册表第 0 个 = 启动器）前台时不画返回键：没有"更上一层"可回。
  const bool show = (id != 0U);
  if (show == back_visible_) {
    return;
  }
  back_visible_ = show;
  if (show) {
    lv_obj_clear_flag(back_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(back_, LV_OBJ_FLAG_HIDDEN);
  }
}

void NavShell::set_foreground_policy(AppSettings settings) noexcept {
  policy_ = settings;
  refresh_policy();
}

bool NavShell::take_home_request() noexcept {
  if (!home_requested_) {
    return false;
  }
  home_requested_ = false;  // 边沿语义：取走即清
  return true;
}

void NavShell::on_back_clicked(lv_event_t* event) noexcept {
  auto* self = static_cast<NavShell*>(lv_event_get_user_data(event));
  if (self == nullptr) {
    return;
  }
  self->home_requested_ = true;
  ++self->home_requests_;
}

void NavShell::on_clock_timer(lv_timer_t* timer) noexcept {
  auto* self = static_cast<NavShell*>(timer->user_data);
  if (self != nullptr) {
    self->refresh_clock();
  }
}

void NavShell::refresh_clock() noexcept {
  char text[8];
  bool valid = false;
  if (context_ != nullptr) {
    const etl::expected<std::uint64_t, Error> epoch = context_->time.epoch_ms();
    if (epoch.has_value()) {
      const std::uint64_t local_ms = epoch.value() + timezone_offset_ms;
      const std::uint64_t minutes = (local_ms / 60000ULL) % 1440ULL;
      std::snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(minutes / 60ULL),
                    static_cast<unsigned>(minutes % 60ULL));
      valid = true;
    }
  }
  if (!valid) {
    std::snprintf(text, sizeof(text), "--:--");
  }
  if (std::strcmp(text, clock_text_) == 0) {
    return;
  }
  std::snprintf(clock_text_, sizeof(clock_text_), "%s", text);
  if (clock_label_ != nullptr) {
    lv_label_set_text(clock_label_, clock_text_);
  }
}

void NavShell::refresh_policy() noexcept {
  char text[24];
  switch (policy_.background) {
    case BackgroundPolicy::tick:
      std::snprintf(text, sizeof(text), "tick:%ums", static_cast<unsigned>(policy_.period_ms));
      break;
    case BackgroundPolicy::own_task:
      // period_ms = 0 在框架里是"跑一轮就结束"（issue 15），状态行照实写 once。
      if (policy_.period_ms == 0U) {
        std::snprintf(text, sizeof(text), "own:once");
      } else {
        std::snprintf(text, sizeof(text), "own:%ums", static_cast<unsigned>(policy_.period_ms));
      }
      break;
    case BackgroundPolicy::suspend:
    default:
      std::snprintf(text, sizeof(text), "悬");
      break;
  }
  if (std::strcmp(text, policy_text_) == 0) {
    return;
  }
  std::snprintf(policy_text_, sizeof(policy_text_), "%s", text);
  if (policy_label_ != nullptr) {
    lv_label_set_text(policy_label_, policy_text_);
  }
}

}  // namespace embark::platform
