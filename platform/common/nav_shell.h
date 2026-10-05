/**
 * 平台共用层 · 框架导航壳（issue 16、ADR 0006）
 *
 * 导航壳是【框架的】，不是 App 的：它挂在 `lv_layer_top()` 上，跨 App 的
 * `lv_scr_load` 持久存在，App 契约一行不改。它只有三件事：
 *
 *   - 状态行：顶部一条（高 design_tokens::status_h），左侧时间、右侧当前前台的
 *     后台策略名。**状态行本身不可点击** —— 它盖在 App 屏幕的最上面，一旦可点就会
 *     把下面 App 的输入全吃掉（lv_obj_pos.c:955 的命中测试会先命中它）。
 *   - 返回键：左上角 22×22 的方块，只在"前台不是主屏"时显示，点击只置一个请求位，
 *     真正的切换由框架在循环边界执行（壳不碰 Framework，耦合只走 IUiPort）。
 *   - 时间：自己起一个 1 s 的 LVGL 定时器刷新（LVGL 定时器在 UI 任务里由
 *     lv_timer_handler 驱动，见 platform/common/lvgl_port.cpp 的 tick 口径）。
 *
 * 真机 RTC 未接：v1 固定按东八区显示（epoch_ms + 8h），取不到时间就画 "--:--"。
 * 时区/时间服务不在 v1 范围（spec §6 只要求"墙钟可观测"）。
 *
 * 线程纪律与 IUiPort 一致：只被唯一 UI 任务碰，不加锁。
 */
#ifndef EMBARK_PLATFORM_NAV_SHELL_H
#define EMBARK_PLATFORM_NAV_SHELL_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/message.h>

namespace embark::platform {

class NavShell final {
 public:
  /// 返回键的几何（宿主合成点击用：ui_demo / ui_tour 点它的中心）。
  static constexpr lv_coord_t back_size = 22;
  static constexpr lv_coord_t back_x = 4;
  static constexpr lv_coord_t back_y = 3;
  static constexpr lv_coord_t back_center_x = back_x + back_size / 2;  // 15
  static constexpr lv_coord_t back_center_y = back_y + back_size / 2;  // 14

  NavShell() noexcept = default;
  ~NavShell() = default;

  NavShell(const NavShell&) = delete;
  NavShell& operator=(const NavShell&) = delete;

  /// 建状态行与返回键。显示设备没就绪返回 not_ready（幂等：重复调用直接 none）。
  [[nodiscard]] Error init(hal::Context& context) noexcept;

  /// 前台变了（框架的首次前台 + 每次切换生效后各一次）。
  void set_foreground(AppId id) noexcept;

  /// 当前前台 App 的后台配置（状态行右半段）。
  void set_foreground_policy(AppSettings settings) noexcept;

  /// 用户按了返回键吗（取走即清；框架在循环边界把它变成 request_home）。
  [[nodiscard]] bool take_home_request() noexcept;

  // --- 观测（验收用）--------------------------------------------------------
  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] bool back_visible() const noexcept { return back_visible_; }
  [[nodiscard]] AppId foreground() const noexcept { return foreground_; }
  [[nodiscard]] const char* clock_text() const noexcept { return clock_text_; }
  [[nodiscard]] const char* policy_text() const noexcept { return policy_text_; }
  [[nodiscard]] std::uint32_t home_requests() const noexcept { return home_requests_; }

 private:
  static void on_back_clicked(lv_event_t* event) noexcept;
  static void on_clock_timer(lv_timer_t* timer) noexcept;

  void refresh_clock() noexcept;
  void refresh_policy() noexcept;

  hal::Context* context_ = nullptr;
  lv_obj_t* bar_ = nullptr;
  lv_obj_t* clock_label_ = nullptr;
  lv_obj_t* policy_label_ = nullptr;
  lv_obj_t* back_ = nullptr;
  lv_timer_t* clock_timer_ = nullptr;

  char clock_text_[8] = "--:--";
  char policy_text_[24] = "悬";

  AppId foreground_ = invalid_app_id;
  AppSettings policy_{};
  std::uint32_t home_requests_ = 0;
  bool back_visible_ = false;
  bool home_requested_ = false;
  bool ready_ = false;
};

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_NAV_SHELL_H */
