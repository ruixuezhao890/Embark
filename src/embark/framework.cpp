/**
 * Embark · Framework 实现（issues/06、07）
 *
 * 顺序与纪律都写死在 include/embark/framework.h 的文件头注释里，这里只补实现细节。
 */
#include <embark/framework.h>

#include <embark/diagnostics.h>

namespace embark {

Framework::Framework(hal::Context& hal, AppRegistry apps, IUiPort* ui,
                     ITaskSpawner* spawner) noexcept
    : hal_(hal), apps_(apps), ui_(ui), spawner_(spawner) {}

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

  // --- 总线装配（issue 07）--------------------------------------------------
  // 每个 App 一个订阅适配器（v1 全收，App 在 onMessage 里按消息 id 自己分发）。
  // 注意：onCreate 在总线装配之前 —— onCreate 里 publish 的消息没人收
  // （会被计为 unknown，WARN 一次），v1 约定：装配消息请放到 onEnter 之后。
  for (std::size_t index = 0; index < apps_.size(); ++index) {
    AppAdapter& adapter = app_adapters_[index];
    adapter.bind(apps_.at(index));
    if (!bus_.subscribe(adapter)) {
      return Error::no_space;  // 满 8 个订阅者（App 数 ≤ max_apps，理论到不了）
    }
  }

  // --- 后台节拍（issue 07）--------------------------------------------------
  // ETL 的 callback_timer 默认关闭（callback_timer.h:602），必须先 enable。
  timers_.enable(true);
  std::size_t timer_index = 0;
  for (std::size_t index = 0; index < apps_.size(); ++index) {
    const AppSettings settings = apps_.at(index)->settings();
    if (settings.background != BackgroundPolicy::tick || settings.period_ms == 0) {
      continue;  // suspend / period 0 = 不跑
    }
    if (timer_index >= max_background_timers) {
      // 理论上到不了：App 数 ≤ max_apps = max_background_timers。
      return Error::no_space;
    }
    BgTrampoline& trampoline = bg_trampolines_[timer_index];
    trampoline.bind(this, static_cast<AppId>(index));
    // 周期以 UI 循环节拍为粒度向上取整（spec §6：后台 tick 在 UI 循环里跑，
    // 精度受 ui_loop_period_ms 限制 —— 宿主 5 ms，真机按自己的循环周期重排）。
    const std::uint32_t period_ticks =
        (settings.period_ms + ui_loop_period_ms - 1U) / ui_loop_period_ms;
    const etl::timer::id::type id =
        timers_.register_timer(trampoline, period_ticks, true);
    timers_.start(id, false);
    ++timer_index;
  }

  // --- own task 逃生舱（issue 07）--------------------------------------------
  std::size_t own_slot = 0;
  for (std::size_t index = 0; index < apps_.size(); ++index) {
    const AppSettings settings = apps_.at(index)->settings();
    if (settings.background != BackgroundPolicy::own_task) {
      continue;
    }
    if (spawner_ == nullptr) {
      return Error::unsupported;  // 平台没给任务创建能力
    }
    if (own_slot >= max_own_tasks) {
      return Error::no_space;  // 静态槽位耗尽
    }
    if (settings.task_stack_words > own_task_stack_words) {
      return Error::no_space;  // App 要的栈超出框架分配的槽位
    }
    OwnTaskArg& arg = own_task_args_[own_slot];
    arg.fw = this;
    arg.app = static_cast<AppId>(index);
    const std::uint16_t stack_words = settings.task_stack_words != 0
                                          ? settings.task_stack_words
                                          : static_cast<std::uint16_t>(own_task_stack_words);
    const Error spawn_error = spawner_->spawn_task(
        apps_.at(index)->name(), &Framework::own_task_entry, &arg, stack_words,
        settings.task_priority);
    if (spawn_error != Error::none) {
      return spawn_error;
    }
    ++own_slot;
  }

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

  // 5. 后台节拍：tick 策略的 App 按各自周期触发 onBackgroundTick（framework 线程）。
  timers_.tick(1);

  // 6. 消息派发：own task 的来信（收件箱抽干，绝不阻塞）经总线广播。
  //    信封本身也是普通消息（cross_task_message_id），App 在 onMessage 里认。
  CrossTaskMessage envelope;
  while (inbox_.pop(envelope)) {
    bus_.publish(envelope);
  }

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

void Framework::fire_background_tick(AppId app_id) noexcept {
  App* const app = apps_.at(app_id);
  if (app != nullptr) {
    app->onBackgroundTick(hal_.time.now_ms());
  }
}

void Framework::own_task_entry(void* argument) noexcept {
  auto* const arg = static_cast<OwnTaskArg*>(argument);
  arg->fw->run_own_task(arg->app);
  // 合同：任务函数不许返回（与 UI 任务同一条纪律，spec §6）。
  ::embark::fatal("own task 入口返回了：任务函数不许返回");
  for (;;) {
  }  // 防御：fatal 若被测试替身接管后返回，也不能继续往下走
}

void Framework::run_own_task(AppId app_id) noexcept {
  App* const app = apps_.at(app_id);
  const std::uint32_t period_ms =
      app != nullptr ? app->settings().period_ms : 0U;
  for (;;) {
    hal_.time.delay_ms(period_ms);
    if (app != nullptr) {
      app->onBackgroundTick(hal_.time.now_ms());
    }
  }
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
  // own task 不停：v1 常驻任务没有退役路径（见 framework.h 文件头），
  // 宿主由 exit_process(_Exit) 兜底回收。
}

}  // namespace embark