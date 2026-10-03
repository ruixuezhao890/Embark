/**
 * Embark 宿主 · UI 端口（issues/06）
 *
 * IUiPort 在宿主上的实现：把 LvglPort（issue 05 的 LVGL 接入）装进框架端口，
 * 并补上框架要求的两个职责：
 *   - lv_timer_handler() 的唯一调用点（spec §6：全工程只有 UI 任务能碰 LVGL）；
 *   - 退出信号（宿主 = 用户关窗 → HostInput::quit_requested）。
 *
 * 装配顺序（由 demo 在 UI 任务里执行）：
 *   1. HostHal::init()                     —— HAL 后端（含 SDL 显示/输入）
 *   2. LvglUiPort::init()（被 Framework::boot 调）—— lv_init + 驱动
 * 所以本端口只管 LVGL；SDL 窗口的生命周期属于 platform/host（HostDisplay/HostInput），
 * 框架不知道 SDL 的存在。
 */
#ifndef EMBARK_PLATFORM_HOST_LVGL_UI_PORT_H
#define EMBARK_PLATFORM_HOST_LVGL_UI_PORT_H

#include <lvgl.h>

#include <embark/ui_port.h>

#include "host_display.h"
#include "host_input.h"
#include "lvgl_port.h"

namespace embark::platform::host {

class LvglUiPort final : public IUiPort {
 public:
  /// 引用在对象存活期间必须有效（宿主是进程级静态，没问题）。
  LvglUiPort(hal::Context& context, HostDisplay& display, HostInput& input) noexcept;

  ~LvglUiPort() override = default;

  LvglUiPort(const LvglUiPort&) = delete;
  LvglUiPort& operator=(const LvglUiPort&) = delete;

  [[nodiscard]] Error init() noexcept override;
  void tick() noexcept override;
  void pump_input() noexcept override;
  void process() noexcept override;
  [[nodiscard]] bool exit_requested() const noexcept override;
  void shutdown() noexcept override;

  /// 观测：把底层 LvglPort 的统计透出来（验收/看板）。
  [[nodiscard]] LvglPort& port() noexcept { return port_; }
  [[nodiscard]] const LvglPort& port() const noexcept { return port_; }

 private:
  hal::Context& context_;
  HostDisplay& display_;
  HostInput& input_;
  LvglPort port_;
  bool ready_ = false;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_LVGL_UI_PORT_H */