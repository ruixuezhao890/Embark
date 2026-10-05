/**
 * 平台共用层 · UI 端口实现（issues/06、11）
 *
 * 本类只有转发与装配，没有任何业务；线程纪律见 include/embark/ui_port.h：
 * 所有方法只被唯一 UI 任务调用，不需要锁。
 */
#include "lvgl_ui_port.h"

#include <embark/log.h>
#include <embark_lvgl_hooks.h>

namespace embark::platform {

LvglUiPort::LvglUiPort(hal::Context& context, ExitQuery exit_query, void* exit_context) noexcept
    : context_(context), exit_query_(exit_query), exit_context_(exit_context), port_(context) {}

Error LvglUiPort::init() noexcept {
  if (ready_) {
    return Error::none;
  }
  // HAL 的显示必须在调用本端口之前就绪（平台在 UI 任务里先做 HAL init）。
  // 输入这边只判"有没有"：`hal::IInput` 没有 is_ready()（只有 init()/poll()），
  // 输入后端是否可用由各平台自己负责 —— LvglPort 遇到坏输入会记一条 ERROR 后继续跑。
  if (context_.display == nullptr || !context_.display->is_ready()) {
    return Error::not_ready;
  }
  const Error error = port_.init();
  if (error == Error::none) {
    ready_ = true;
    // 导航壳不是核心（没有它框架照跑），失败只记 ERROR、不回滚 ready_：
    // 无界面平台/CI 单测（FakeUiPort）根本不会走到这，真机只影响状态行与返回键。
    const Error shell_error = nav_shell_.init(context_);
    if (shell_error != Error::none) {
      ELOG_ERROR("导航壳 init 失败（{}），状态行/返回键不可用", shell_error);
    }
  }
  return error;
}

void LvglUiPort::tick() noexcept {
  port_.tick();
}

void LvglUiPort::pump_input() noexcept {
  port_.pump_input();
}

void LvglUiPort::set_foreground(AppId id) noexcept {
  nav_shell_.set_foreground(id);
}

void LvglUiPort::set_foreground_policy(AppSettings settings) noexcept {
  nav_shell_.set_foreground_policy(settings);
}

bool LvglUiPort::take_home_request() noexcept {
  return nav_shell_.take_home_request();
}

void LvglUiPort::process() noexcept {
  // 全工程 lv_timer_handler() 的唯一调用点（spec §6）：
  // LVGL 的定时器/重绘/输入事件回调都在这一句里面发生，前台 App 的点击回调
  // 就是从这蹦出来的。别的地方再调它，就违反了"只有 UI 任务碰 LVGL"。
  lv_timer_handler();
}

bool LvglUiPort::exit_requested() const noexcept {
  return exit_query_ != nullptr && exit_query_(exit_context_);
}

void LvglUiPort::shutdown() noexcept {
  // LVGL 8.3.11 在 LV_MEM_CUSTOM=1 下没有 lv_deinit（lv_obj.h:206-214 把它藏起来了），
  // 所以这里做不了完整的 LVGL 收尾；框架的退出路径走到这就够了 —— 宿主进程随后
  // 由 exit_process()（_Exit）结束，真机则由 esp_restart() 重启。想验收"LVGL 还占
  // 多少"用 embark_lvgl_outstanding_bytes()：这行是宿主与真机共用的同一句。
  if (ready_) {
    ELOG_INFO("UI 端口收尾：LVGL 未回收 {} 字节（预算 {}）", embark_lvgl_outstanding_bytes(),
              embark_lvgl_budget_bytes());
    ready_ = false;
  }
}

}  // namespace embark::platform
