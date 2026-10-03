/**
 * Embark 宿主 · UI 端口实现（issues/06）
 *
 * 本类只有转发与装配，没有任何业务；线程纪律见 include/embark/ui_port.h：
 * 所有方法只被唯一 UI 任务调用，不需要锁。
 */
#include "lvgl_ui_port.h"

#include <embark/log.h>
#include <embark_limits.h>

#include "host_lvgl_mem.h"

namespace embark::platform::host {

LvglUiPort::LvglUiPort(hal::Context& context, HostDisplay& display,
                       HostInput& input) noexcept
    : context_(context), display_(display), input_(input), port_(context) {}

Error LvglUiPort::init() noexcept {
  if (ready_) {
    return Error::none;
  }
  // HAL 的显示/输入必须在调用本端口之前就绪（demo 在 UI 任务里先 HostHal::init）。
  if (!display_.is_ready() || !input_.is_ready()) {
    return Error::not_ready;
  }
  const Error error = port_.init();
  if (error == Error::none) {
    ready_ = true;
  }
  return error;
}

void LvglUiPort::tick() noexcept {
  port_.tick();
}

void LvglUiPort::pump_input() noexcept {
  port_.pump_input();
}

void LvglUiPort::process() noexcept {
  // 全工程 lv_timer_handler() 的唯一调用点（spec §6）：
  // LVGL 的定时器/重绘/输入事件回调都在这一句里面发生，前台 App 的点击回调
  // 就是从这蹦出来的。别的地方再调它，就违反了"只有 UI 任务碰 LVGL"。
  lv_timer_handler();
}

bool LvglUiPort::exit_requested() const noexcept {
  return input_.quit_requested();
}

void LvglUiPort::shutdown() noexcept {
  // LVGL 8.3.11 在 LV_MEM_CUSTOM=1 下没有 lv_deinit（lv_obj.h:206-214 把它藏起来了），
  // 所以这里做不了完整的 LVGL 收尾；框架的退出路径走到这就够了 —— 宿主进程随后
  // 由 exit_process()（_Exit）结束，SDL/窗口资源由操作系统回收。想验收"LVGL 还占
  // 多少"用 platform_host::lvgl_outstanding_bytes()。
  if (ready_) {
    ELOG_INFO("UI 端口收尾：LVGL 未回收 {} 字节（预算 {}）", lvgl_outstanding_bytes(),
              embark::lvgl_alloc_budget_bytes);
    ready_ = false;
  }
}

}  // namespace embark::platform::host