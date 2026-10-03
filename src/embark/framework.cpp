/**
 * Embark · Framework 实现（issues/06）
 *
 * 顺序与纪律都写死在 include/embark/framework.h 的文件头注释里，这里只补实现细节。
 */
#include <embark/framework.h>

namespace embark {

Framework::Framework(hal::Context& hal, AppRegistry apps, IUiPort* ui) noexcept
    : hal_(hal), apps_(apps), ui_(ui) {}

Error Framework::boot() noexcept {
  if (booted_) {
    return Error::none;
  }
  if (apps_.empty()) {
    // 没有 App 的框架没有默认前台，循环也没有意义 —— 这是装配错误。
    return Error::invalid_argument;
  }
  if (apps_.size() > max_apps) {
    // 编译期有 static_assert（app_registry.h），这里只是防运行时构造的非法表。
    return Error::invalid_argument;
  }
  if (ui_ != nullptr) {
    const Error ui_error = ui_->init();
    if (ui_error != Error::none) {
      return ui_error;
    }
  }

  for (std::size_t index = 0; index < apps_.size(); ++index) {
    apps_.at(index)->onCreate(*this);
  }

  foreground_ = 0;
  pending_ = 0;
  entered_[0] = true;
  apps_.at(0)->onEnter();

  booted_ = true;
  return Error::none;
}

void Framework::step() noexcept {
  if (!booted_ || shutdown_) {
    return;
  }

  if (ui_ != nullptr) {
    ui_->tick();         // 1. UI 时间轴（LVGL tick，必须在任何界面工作之前）
    ui_->pump_input();   // 2. 输入事件处理（抽干 HAL 输入队列；事件回调在 process 里）
  }
  apply_pending_switch();  // 3. 循环边界：让前台切换请求生效（钩子顺序可预期）
  if (ui_ != nullptr) {
    ui_->process();      // 4. 界面工作（lv_timer_handler → 前台 App 的 LVGL 回调）
  }
  // 5. 后台 tick 调度、6. 消息派发 —— issue 07 在这里接进来（spec §6 的顺序不变）。

  ++frames_;
}

Error Framework::request_switch(AppId id) noexcept {
  if (!apps_.valid(id)) {
    return Error::not_found;
  }
  pending_ = id;
  return Error::none;
}

Error Framework::request_switch(const char* name) noexcept {
  const AppId id = apps_.find(name);
  if (id == invalid_app_id) {
    return Error::not_found;
  }
  return request_switch(id);
}

void Framework::apply_pending_switch() noexcept {
  if (pending_ == foreground_) {
    return;  // 切到当前前台 = no-op（含 boot 前的非法状态）
  }

  App* const from = apps_.at(foreground_);
  App* const to = apps_.at(pending_);

  if (from != nullptr) {
    from->onPause();
  }
  foreground_ = pending_;
  if (to != nullptr) {
    if (entered_[foreground_]) {
      to->onResume();  // 已经不是第一次当前台
    } else {
      to->onEnter();  // 第一次当前台
      entered_[foreground_] = true;
    }
  }
  ++switches_;
}

void Framework::shutdown() noexcept {
  if (!booted_ || shutdown_) {
    return;
  }
  shutdown_ = true;

  if (foreground_ != invalid_app_id) {
    if (App* const foreground = apps_.at(foreground_); foreground != nullptr) {
      foreground->onPause();
    }
  }

  for (std::size_t index = 0; index < apps_.size(); ++index) {
    apps_.at(index)->onExit();  // 注册顺序 = 收尾顺序（与 onCreate 一致）
  }

  if (ui_ != nullptr) {
    ui_->shutdown();
  }
}

}  // namespace embark