/**
 * Embark · Framework（issues/06、07）
 *
 * spec §5/§6/§7 的执行核心：装配（boot）、唯一 UI 循环（step）、前后台切换
 * （request_switch）、消息投递（publish/post）、后台节拍（tick/own_task）、
 * 退出路径（shutdown）。
 *
 *   - boot()     装配：注入的 UI 端口 init → 按注册顺序跑每个 App 的 onCreate
 *                → 默认前台（注册表第 0 个）onEnter → 总线装配（每个 App 一个
 *                订阅适配器，v1 全收）→ 后台节拍（tick 策略的 App 每人一个
 *                周期定时器）→ 后台任务（own_task 策略 → ITaskSpawner，每个
 *                任务一份持有者侧记录 own_tasks_，见下面的 own task 生命周期）。
 *                失败时通过返回值报错，不进入循环（此时任何 App 的 onExit
 *                都不会被调用）。幂等：重复调用直接返回 none。
 *   - step()     唯一的循环体，一次一帧。顺序是 spec §6 定死的：
 *                1 UI tick → 2 输入抽干 → 3 （循环边界上的）切换生效 →
 *                4 界面工作 → 5 后台节拍（tick 策略） → 6 消息派发
 *                （own task 收件箱抽干 → 总线广播） → 7 own task 回收
 *                （issue 15：入口已返回的任务由持有者归还槽位） → ++frames。
 *   - request_switch(id)  只登记"想要哪个 App 当前台"；真正执行在下一个循环
 *                边界（step 开头），切换顺序：旧前台 onPause → 新前台
 *                onEnter（第一次）/ onResume（再次）。切换决策与时机归框架，
 *                App 永远无权直接改前台。
 *   - publish/post  消息投递（spec §7）：publish 是总线广播，只能在框架线程
 *                （UI 任务 / 后台 tick / App 生命周期钩子）调用；own task 里
 *                发消息必须走 post()（信封入收件箱，step 的派发段再广播，
 *                锁在队列内部，绝不阻塞 UI 任务）。
 *   - shutdown() 用户关窗 / 平台关机路径：前台 onPause + 全部 App 按注册顺序
 *                onExit + UI 端口 shutdown。之后不要再 step。own task 不做终止：
 *                入口已返回的由 step 的回收段归还槽位，仍在跑的只随进程退出
 *                （宿主由 _Exit 兜底，spec §6）。
 *
 * own task 生命周期（issue 15，spec §6/§10）：任务入口**允许返回** —— 返回 = 任务
 * 自行结束，平台把它停稳后标记槽位 finished，持有者（唯一 UI 任务）用
 * reap_finished_own_tasks() 归还槽位；归还后槽位可再次创建。跑完一轮就结束的
 * 任务由 App 用 period_ms == 0 声明（见 run_own_task）。
 *
 * 零堆纪律：本类没有堆分配，所有容量来自 config/embark_limits.h 的编译期常量。
 * 线程纪律：step 与 shutdown 必须只在唯一 UI 任务上调用（spec §6），本类无锁；
 *           post() 例外 —— 它给 own task 用，锁在 MessageQueue 内部。
 */
#ifndef EMBARK_FRAMEWORK_H
#define EMBARK_FRAMEWORK_H

#include <cstddef>
#include <cstdint>

#include <middleware/etl/array.h>
#include <middleware/etl/callback_timer.h>

#include <embark/app.h>
#include <embark/app_registry.h>
#include <embark/bus.h>
#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/message_queue.h>
#include <embark/task_spawner.h>
#include <embark/ui_port.h>
#include <embark_limits.h>

namespace embark {

class Framework {
 public:
  /// hal 与 apps 在对象存活期间必须有效（宿主是进程级静态，没问题）。
  /// ui 可空（无界面平台/测试）：没有 UI 时 step 只做切换与后续系统工作。
  /// spawner 可空：存在 own_task 策略的 App 时 boot 返回 Error::unsupported。
  Framework(hal::Context& hal, AppRegistry apps, IUiPort* ui = nullptr,
            ITaskSpawner* spawner = nullptr) noexcept;

  Framework(const Framework&) = delete;
  Framework& operator=(const Framework&) = delete;

  ~Framework() = default;

  /// 装配（见文件头）。幂等：重复调用直接返回 none。失败返回具体 Error。
  [[nodiscard]] Error boot() noexcept;

  /// 一次帧 = 唯一 UI 循环的一步。boot 成功后调用；shutdown 后不要再调。
  void step() noexcept;

  /// 前台切换请求：只登记，下一个 step 边界生效。返回 NotFound = 编号/名字无效；
  /// 请求切到当前前台 = no-op（返回 none，不做任何钩子）。
  [[nodiscard]] Error request_switch(AppId id) noexcept;
  [[nodiscard]] Error request_switch(const char* name) noexcept;

  /// 退出路径（见文件头）。幂等；之后不要再 step。
  void shutdown() noexcept;

  // --- 消息与后台（issues/07）-----------------------------------------------

  /// 发消息：总线广播。只能在框架线程调用（UI 任务 / tick / App 生命周期钩子）；
  /// 后台 own task 里请走 post()。
  void publish(const etl::imessage& message) noexcept { bus_.publish(message); }

  /// 跨任务投递：own task 把信封交给框架（收件箱，锁在队列内部，绝不阻塞）。
  /// UI 循环在 step 的派发段把收件箱抽干，经总线广播给每个 App 的 onMessage。
  void post(const CrossTaskMessage& message) noexcept { inbox_.push(message); }

  /// 总线（观测：published / unknown / subscriber_count）。
  [[nodiscard]] const Bus& bus() const noexcept { return bus_; }

  /// 收件箱累计溢出次数（own task 发太快、UI 来不及消化的信号）。
  [[nodiscard]] std::uint32_t inbox_overflows() const noexcept { return inbox_.overflows(); }

  // --- own task 生命周期（issue 15）------------------------------------------

  /// 运行期创建 own task。持有者 = 唯一 UI 任务（boot/step 也在它上面），
  /// 所以只允许在 boot 之后、shutdown 之前、UI 任务里调用。
  /// 只有声明 own_task 策略的 App 能这么用；同一个 App 同时只允许一个任务。
  /// 返回：not_found = 编号无效；not_ready = 还没 boot / 已经 shutdown；
  ///       unsupported = 平台没给 spawner，或该 App 不是 own_task 策略；
  ///       no_space = 栈深超槽容量，或没有空闲槽（池满）；busy = 该 App 已有任务。
  /// 任务入口返回后，槽位由 step 的回收段自动归还，之后可以再创建。
  [[nodiscard]] Error spawn_own_task(AppId id) noexcept;

  /// 回收入口已经返回、平台也已把它停稳的 own task，返回本次回收数（0 = 没有
  /// 可回收的）。还在跑的任务返回 busy —— 这不是错误，下一帧再试即可。
  /// step 每帧自动调一次；单独调用是为了显式控制/验收。
  [[nodiscard]] std::size_t reap_finished_own_tasks() noexcept;

  /// 累计创建 / 累计回收的 own task 数（观测/验收用）。
  [[nodiscard]] std::uint32_t own_tasks_spawned() const noexcept { return own_tasks_spawned_; }
  [[nodiscard]] std::uint32_t own_tasks_released() const noexcept { return own_tasks_released_; }

  // --- 查询 ---------------------------------------------------------------

  [[nodiscard]] const AppRegistry& apps() const noexcept { return apps_; }
  [[nodiscard]] bool booted() const noexcept { return booted_; }

  /// 当前前台 App 编号（boot 前 = invalid_app_id）。
  [[nodiscard]] AppId foreground() const noexcept { return foreground_; }

  /// 已登记、待生效的切换目标（没有则 == foreground()）。
  [[nodiscard]] AppId pending_foreground() const noexcept { return pending_; }

  /// 有没有还没生效的切换请求。
  [[nodiscard]] bool switch_pending() const noexcept { return pending_ != foreground_; }

  /// 已经跑过的帧数（观测/验收用）。
  [[nodiscard]] std::uint32_t frames() const noexcept { return frames_; }

  /// 已经发生过多少次前台切换（观测/验收用；同一对 App 反复切也会累计）。
  [[nodiscard]] std::uint32_t switches() const noexcept { return switches_; }

  /// UI 层是否请求退出（宿主 = 关窗；没有 UI 端口时恒为 false）。
  [[nodiscard]] bool exit_requested() const noexcept {
    return ui_ != nullptr && ui_->exit_requested();
  }

  /// 随便查哪个 App（越界返回 nullptr）。
  [[nodiscard]] App* app(AppId id) noexcept { return apps_.at(id); }

  /// 反查 App 编号（不在注册表里返回 invalid_app_id）。
  [[nodiscard]] AppId id_of(const App& app) const noexcept { return apps_.id_of(app); }

  /// HAL 上下文 —— onCreate 里 App 拿硬件能力的唯一入口（spec：注入，不 include 平台）。
  [[nodiscard]] hal::Context& hal() noexcept { return hal_; }

 private:
  /// 让待生效的切换落地（只在 step 的固定边界调用，保证钩子顺序可预期）。
  void apply_pending_switch() noexcept;

  /// tick 策略的周期性回调（framework 线程；BgTrampoline 转接来）。
  void fire_background_tick(AppId app_id) noexcept;

  // --- own task 逃生舱（spec §6）-------------------------------------------

  /// 传给 ITaskSpawner 的参数（生命周期 = Framework 对象，平台只转发指针）。
  /// 平台把它的地址拷进任务槽（PoolTask::argument），任务启动后一直有效。
  struct OwnTaskArg {
    Framework* fw = nullptr;
    AppId app = invalid_app_id;
  };

  /// 持有者侧的一份账：平台句柄 + 参数 + 是否在跑（池的槽位由 spawner 决定，
  /// 这里只按"哪个 App"记账，两者容量同口径 = max_own_tasks）。
  struct OwnTaskRecord {
    TaskToken token{};
    OwnTaskArg arg{};
    bool active = false;
  };

  /// own task 的入口：跑完 App 的后台体就**返回**（返回 = 任务结束，不是错误）。
  /// 返回后平台 trampoline 把槽位标 finished 并 park 住，持有者在 step 里回收。
  static void own_task_entry(void* argument) noexcept;

  /// 后台任务体：period_ms > 0 = 常驻循环（delay + onBackgroundTick，永不返回）；
  /// period_ms == 0 = 一次性任务（跑一轮 onBackgroundTick 就结束）。
  void run_own_task(AppId app_id) noexcept;

  /// spawn_own_task 的实现：boot 也走它，所以不查 booted_/shutdown_。
  [[nodiscard]] Error spawn_own_task_internal(AppId id) noexcept;

  /// 该 App 有没有在跑的 own task（返回记录下标；没有返回 own_tasks_.size()）。
  [[nodiscard]] std::size_t own_task_index_of(AppId id) const noexcept;

  /// 找一个空闲记录（满了返回 own_tasks_.size()）。
  [[nodiscard]] std::size_t find_free_own_task() const noexcept;

  // --- App 到总线的桥 -------------------------------------------------------

  /// 每个 App 一个订阅适配器（id 恒 = MESSAGE_ROUTER；v1 全收，过滤留给 v1.1）。
  /// 消息到达 → App::onMessage（App 自己按 get_message_id() 分发）。
  class AppAdapter : public etl::imessage_router {
   public:
    AppAdapter() noexcept : etl::imessage_router(etl::imessage_router::MESSAGE_ROUTER) {}

    void bind(App* app) noexcept { app_ = app; }

    void receive(const etl::imessage& message) override {
      if (app_ != nullptr) {
        app_->onMessage(message);
      }
    }
    bool accepts(etl::message_id_t) const override { return true; }
    bool is_null_router() const override { return false; }
    bool is_producer() const override { return false; }
    bool is_consumer() const override { return true; }

   private:
    App* app_ = nullptr;
  };

  /// 后台节拍的桥：etl::callback_timer 只收 etl::ifunction（const operator()），
  /// 这里转接到 Framework::fire_background_tick。
  class BgTrampoline : public etl::ifunction<void> {
   public:
    void bind(Framework* fw, AppId app_id) noexcept {
      fw_ = fw;
      app_id_ = app_id;
    }
    void operator()() const override { fw_->fire_background_tick(app_id_); }

   private:
    Framework* fw_ = nullptr;
    AppId app_id_ = invalid_app_id;
  };

  hal::Context& hal_;
  AppRegistry apps_;
  IUiPort* ui_;
  ITaskSpawner* spawner_ = nullptr;

  bool booted_ = false;
  bool shutdown_ = false;
  AppId foreground_ = invalid_app_id;
  AppId pending_ = invalid_app_id;

  /// 每个 App "是否进过前台"（决定下次回来 onEnter 还是 onResume）。
  etl::array<bool, max_apps> entered_ = {};

  std::uint32_t frames_ = 0;
  std::uint32_t switches_ = 0;

  // --- 消息与后台（issues/07）-----------------------------------------------

  Bus bus_;
  etl::array<AppAdapter, max_apps> app_adapters_ = {};

  etl::callback_timer<max_background_timers> timers_;
  etl::array<BgTrampoline, max_background_timers> bg_trampolines_ = {};

  /// own task → UI 的收件箱（单生产者单消费者；锁在队列内部）。
  MessageQueue<CrossTaskMessage, message_queue_depth> inbox_;

  /// 每个 own task 一份记录（持有者侧的账；空闲记录的 arg 不会被任何人引用）。
  etl::array<OwnTaskRecord, max_own_tasks> own_tasks_ = {};
  std::uint32_t own_tasks_spawned_ = 0;
  std::uint32_t own_tasks_released_ = 0;
};

}  // namespace embark

#endif /* EMBARK_FRAMEWORK_H */