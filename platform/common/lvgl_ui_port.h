/**
 * 平台共用层 · UI 端口（issues/06，issue 11 从 platform/host 提上来共用）
 *
 * IUiPort 的共用实现：把 LvglPort（LVGL 接入）装进框架端口，并补上框架要求的两个职责：
 *   - lv_timer_handler() 的唯一调用点（spec §6：全工程只有 UI 任务能碰 LVGL）；
 *   - 退出信号。`hal::IInput` 只有 init()/poll()，没有"退出请求"这个概念 ——
 *     宿主是用户关窗、真机可能是某个按键或压根没有 —— 所以这里收一个平台注入的回调，
 *     而不是把某个平台的类型硬塞进共用层。
 *
 * 装配顺序（由平台在 UI 任务里执行）：
 *   1. 平台 HAL 聚合 init()          —— 显示/输入后端就绪
 *   2. LvglUiPort::init()（被 Framework::boot 调）—— lv_init + 驱动注册
 * 窗口/外设的生命周期属于各平台后端（HostDisplay/HostInput、Esp32Display/Esp32Input），
 * 框架不知道它们的存在。
 */
#ifndef EMBARK_PLATFORM_LVGL_UI_PORT_H
#define EMBARK_PLATFORM_LVGL_UI_PORT_H

#include <embark/ui_port.h>

#include "lvgl_port.h"

namespace embark::platform {

class LvglUiPort final : public IUiPort {
 public:
  /// 退出查询：返回 true 表示"用户要求收尾"，框架会在下一个 step 边界走 shutdown。
  using ExitQuery = bool (*)(void* context) noexcept;

  /// 引用在对象存活期间必须有效（宿主是进程级静态，真机是 .bss 里的全局对象）。
  /// exit_query 为空表示本平台没有退出信号（真机 v1 就是这样：一直跑）。
  explicit LvglUiPort(hal::Context& context, ExitQuery exit_query = nullptr,
                      void* exit_context = nullptr) noexcept;

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
  ExitQuery exit_query_ = nullptr;
  void* exit_context_ = nullptr;
  LvglPort port_;
  bool ready_ = false;
};

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_LVGL_UI_PORT_H */
