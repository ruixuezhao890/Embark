/**
 * Embark · Framework（issues/06）
 *
 * spec §5/§6 的执行核心：装配（boot）、唯一 UI 循环（step）、前后台切换
 * （request_switch）、退出路径（shutdown）。
 *
 *   - boot()     装配：注入的 UI 端口 init → 按注册顺序跑每个 App 的 onCreate
 *                → 默认前台（注册表第 0 个）onEnter。失败时通过返回值报错，
 *                不进入循环（此时任何 App 的 onExit 都不会被调用）。
 *   - step()     唯一的循环体，一次一帧。顺序是 spec §6 定死的：
 *                UI tick → 输入抽干 → （循环边界上的）切换生效 → 界面工作。
 *                后台 tick 调度与消息派发在 issue 07 接进来（接在同一条循环里）。
 *   - request_switch(id)  只登记"想要哪个 App 当前台"；真正执行在下一个循环
 *                边界（step 开头），切换顺序：旧前台 onPause → 新前台
 *                onEnter（第一次）/ onResume（再次）。切换决策与时机归框架，
 *                App 永远无权直接改前台。
 *   - shutdown() 用户关窗 / 平台关机路径：前台 onPause + 全部 App 按注册顺序
 *                onExit + UI 端口 shutdown。之后不要再 step。
 *
 * 零堆纪律：本类没有堆分配，所有容量来自 config/embark_limits.h 的编译期常量。
 * 线程纪律：step 与 shutdown 必须只在唯一 UI 任务上调用（spec §6），本类无锁。
 */
#ifndef EMBARK_FRAMEWORK_H
#define EMBARK_FRAMEWORK_H

#include <cstdint>

#include <middleware/etl/array.h>

#include <embark/app.h>
#include <embark/app_registry.h>
#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/ui_port.h>
#include <embark_limits.h>

namespace embark {

class Framework {
 public:
  /// hal 与 apps 在对象存活期间必须有效（宿主是进程级静态，没问题）。
  /// ui 可空（无界面平台/测试）：没有 UI 时 step 只做切换与后续系统工作。
  Framework(hal::Context& hal, AppRegistry apps, IUiPort* ui = nullptr) noexcept;

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

  // --- 查询 ---------------------------------------------------------------

  [[nodiscard]] const AppRegistry& apps() const noexcept { return apps_; }
  [[nodiscard]] bool booted() const noexcept { return booted_; }

  /// 当前前台 App 编号（boot 前 = invalid_app_id）。
  [[nodiscard]] AppId foreground() const noexcept { return foreground_; }

  /// 已登记、待生效的切换目标（没有则 == foreground()）。
  [[nodiscard]] AppId pending_foreground() const noexcept { return pending_; }

  /// 有没有还没生效的切换请求。
  [[nodiscard]] bool switch_pending() const noexcept {
    return pending_ != foreground_;
  }

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
  [[nodiscard]] AppId id_of(const App& app) const noexcept {
    return apps_.id_of(app);
  }

  /// HAL 上下文 —— onCreate 里 App 拿硬件能力的唯一入口（spec：注入，不 include 平台）。
  [[nodiscard]] hal::Context& hal() noexcept { return hal_; }

 private:
  /// 让待生效的切换落地（只在 step 的固定边界调用，保证钩子顺序可预期）。
  void apply_pending_switch() noexcept;

  hal::Context& hal_;
  AppRegistry apps_;
  IUiPort* ui_;

  bool booted_ = false;
  bool shutdown_ = false;
  AppId foreground_ = invalid_app_id;
  AppId pending_ = invalid_app_id;

  /// 每个 App "是否进过前台"（决定下次回来 onEnter 还是 onResume）。
  etl::array<bool, max_apps> entered_ = {};

  std::uint32_t frames_ = 0;
  std::uint32_t switches_ = 0;
};

}  // namespace embark

#endif /* EMBARK_FRAMEWORK_H */