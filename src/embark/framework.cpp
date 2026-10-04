/**
 * Embark · Framework 实现（issues/06、07）
 *
 * 顺序与纪律都写死在 include/embark/framework.h 的文件头注释里，这里只补实现细节。
 */
#include <embark/framework.h>

#include <embark/log.h>

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

  // --- 后台配置一览（issue 13）----------------------------------------------
  // 每个 App 的 AppSettings 整条打出来：字段名与取值名都来自类型声明本身
  // （app.h 的 E_FMT_DERIVE / E_FMT_DERIVE_ENUM），以后加字段不用改这一行。
  for (std::size_t index = 0; index < apps_.size(); ++index) {
    const App* app = apps_.at(index);
    ELOG_INFO("App {} 后台配置 {}", app->name(), app->settings());
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
    const etl::timer::id::type id = timers_.register_timer(trampoline, period_ticks, true);
    timers_.start(id, false);
    ++timer_index;
  }

  // --- own task 逃生舱（issues/07、15）---------------------------------------
  // boot 只把声明了 own_task 策略的 App 装起来：创建细节与失败口径都在
  // spawn_own_task_internal 里 —— 运行期的 spawn_own_task 走的是同一条路（issue 15）。
  for (std::size_t index = 0; index < apps_.size(); ++index) {
    const AppSettings settings = apps_.at(index)->settings();
    if (settings.background != BackgroundPolicy::own_task) {
      continue;
    }
    const Error spawn_error = spawn_own_task_internal(static_cast<AppId>(index));
    if (spawn_error != Error::none) {
      return spawn_error;
    }
  }

  booted_ = true;
  return Error::none;
}

void Framework::step() noexcept {
  if (!booted_ || shutdown_) {
    return;
  }

  if (ui_ != nullptr) {
    ui_->tick();  // 1. UI 时间轴（LVGL tick，必须在任何界面工作之前）
    ui_->pump_input();  // 2. 输入事件处理（抽干 HAL 输入队列；事件回调在 process 里）
  }
  apply_pending_switch();  // 3. 循环边界：让前台切换请求生效（钩子顺序可预期）
  if (ui_ != nullptr) {
    ui_->process();  // 4. 界面工作（lv_timer_handler → 前台 App 的 LVGL 回调）
  }

  // 5. 后台节拍：tick 策略的 App 按各自周期触发 onBackgroundTick（framework 线程）。
  timers_.tick(1);

  // 6. 消息派发：own task 的来信（收件箱抽干，绝不阻塞）经总线广播。
  //    信封本身也是普通消息（cross_task_message_id），App 在 onMessage 里认。
  CrossTaskMessage envelope;
  while (inbox_.pop(envelope)) {
    bus_.publish(envelope);
  }

  // 7. own task 回收（issue 15）：入口已返回、平台已停稳的任务在这里把槽位还给池。
  //    回收责任在持有者（就是本任务）：池只记账，绝不去删一个还在跑的任务。
  (void)reap_finished_own_tasks();  // 回收数是观测值：step 里不关心本次收了几个

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
  if (arg == nullptr || arg->fw == nullptr) {
    return;  // 防御：参数不合法就直接结束（平台会停稳它，持有者回收）
  }
  // 合同（issue 15）：任务入口**允许返回** —— 返回 = 任务自行结束，不是错误。
  // 返回之后平台 trampoline 记 finished 并 park 住，持有者在 step 的回收段还槽位。
  arg->fw->run_own_task(arg->app);
}

void Framework::run_own_task(AppId app_id) noexcept {
  App* const app = apps_.at(app_id);
  if (app == nullptr) {
    return;  // 防御：编号无效直接结束（onCreate 之后不该发生）
  }
  const std::uint32_t period_ms = app->settings().period_ms;
  if (period_ms == 0U) {
    // 一次性任务：跑一轮就结束（返回 = 任务结束，见 framework.h 文件头）。
    app->onBackgroundTick(hal_.time.now_ms());
    return;
  }
  for (;;) {
    hal_.time.delay_ms(period_ms);
    app->onBackgroundTick(hal_.time.now_ms());
  }
}

Error Framework::spawn_own_task(AppId id) noexcept {
  if (!booted_ || shutdown_) {
    return Error::not_ready;  // 持有者的生命周期之外（boot 前 / shutdown 后）
  }
  return spawn_own_task_internal(id);
}

Error Framework::spawn_own_task_internal(AppId id) noexcept {
  if (!apps_.valid(id)) {
    return Error::not_found;
  }
  App* const app = apps_.at(id);
  const AppSettings settings = app->settings();
  if (settings.background != BackgroundPolicy::own_task) {
    return Error::unsupported;  // 这个 App 没声明 own_task 策略
  }
  if (spawner_ == nullptr) {
    return Error::unsupported;  // 平台没给任务创建能力
  }
  if (own_task_index_of(id) != own_tasks_.size()) {
    return Error::busy;  // 同一个 App 同时只允许一个任务
  }
  const std::size_t index = find_free_own_task();
  if (index == own_tasks_.size()) {
    return Error::no_space;  // 记录满 = 并发上限（与池同口径 = max_own_tasks）
  }
  const std::uint16_t stack_words = settings.task_stack_words != 0
                                        ? settings.task_stack_words
                                        : static_cast<std::uint16_t>(own_task_stack_words);
  // 参数必须在 spawn 之前写好：真机上任务落在另一个核，建好就可能开跑并读它。
  OwnTaskRecord& record = own_tasks_[index];
  record.arg.fw = this;
  record.arg.app = id;
  const etl::expected<TaskToken, Error> spawned = spawner_->spawn_task(
      app->name(), &Framework::own_task_entry, &record.arg, stack_words, settings.task_priority);
  if (!spawned.has_value()) {
    record.arg = OwnTaskArg{};  // 失败回滚：空闲记录里不留 this 指针
    return spawned.error();
  }
  record.token = *spawned;
  record.active = true;
  ++own_tasks_spawned_;
  return Error::none;
}

std::size_t Framework::reap_finished_own_tasks() noexcept {
  if (spawner_ == nullptr) {
    return 0U;  // 没有记录，也就没有回收（防御：spawner 为空时记录必然全空）
  }
  std::size_t released = 0U;
  for (std::size_t index = 0; index < own_tasks_.size(); ++index) {
    OwnTaskRecord& record = own_tasks_[index];
    if (!record.active) {
      continue;
    }
    const Error release_error = spawner_->release_task(record.token);
    if (release_error != Error::none) {
      continue;  // busy（还在跑 / 还没停稳）不是错误：下一帧再看
    }
    record = OwnTaskRecord{};  // 句柄、参数一起清掉；token 归零 = 无效句柄
    ++released;
    ++own_tasks_released_;
  }
  return released;
}

std::size_t Framework::own_task_index_of(AppId id) const noexcept {
  for (std::size_t index = 0; index < own_tasks_.size(); ++index) {
    if (own_tasks_[index].active && own_tasks_[index].arg.app == id) {
      return index;
    }
  }
  return own_tasks_.size();
}

std::size_t Framework::find_free_own_task() const noexcept {
  for (std::size_t index = 0; index < own_tasks_.size(); ++index) {
    if (!own_tasks_[index].active) {
      return index;
    }
  }
  return own_tasks_.size();
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
  // own task 不做终止（issue 15）：入口已返回的槽位本来由 step 的回收段归还，
  // 但 shutdown 之后不再 step，所以它们会一直占着槽位 —— 进程退出时随 .bss 一起没；
  // 仍在跑的任务同样只随进程退出（宿主由 exit_process(_Exit) 兜底，见 framework.h 文件头）。
}

}  // namespace embark