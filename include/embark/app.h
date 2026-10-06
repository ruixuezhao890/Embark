/**
 * Embark · App 契约（issues/06）
 *
 * spec §5 的落地。要点：
 *   - 一个类实现 App + 编译期静态注册（零堆），注册顺序即默认前台；
 *   - 前后台语义：前台 = 输入焦点 + 渲染权 + 事件循环权，全局至多一个；
 *     切换只能由框架执行（Framework::request_switch），App 只发请求；
 *   - UI 生命周期自决：框架只发 onPause/onResume，不创建/不销毁/不隐藏
 *     App 的 LVGL 对象（App 自己的屏幕自己管，在 onCreate 里建、onEnter/
 *     onResume 里 lv_scr_load）；
 *   - onEnter 只在"第一次成为前台"时触发，之后再次回到前台走 onResume；
 *   - v1 常驻：没有任何退役/销毁 App 的路径，onExit 只在关机路径触发。
 *
 * 钩子集合 = spec §5 + 前台节拍 onForegroundTick（issue 19 / ADR 0008，为 EEZ
 * Studio 适配引入）。除此之外不再增加。
 *
 * 打印（issue 13）：BackgroundPolicy 与 AppSettings 用 efmt 的派生宏声明，框架在装配
 * 期会把每个 App 的后台配置整条打进日志（src/embark/framework.cpp 的 boot）：
 *
 *   App clock 后台配置 { background = BackgroundPolicy::tick, period_ms = 100, ... }
 */
#ifndef EMBARK_APP_H
#define EMBARK_APP_H

#include <cstdint>

#include <embark/design_tokens.h>
#include <embark/message.h>
#include <middleware/efmt/core/format.hpp>

namespace embark {

class Framework;

// AppId / invalid_app_id 定义在 <embark/message.h>（消息投递也用它们），见下。

/// 后台策略（spec §5、§6）：每 App 一个配置，App 自己声明自己是哪种后台。
E_FMT_DERIVE_ENUM(enum class BackgroundPolicy
                  : std::uint8_t{
                      suspend = 0,  ///< 完全不跑（默认）
                      tick = 1,  ///< 按 period_ms 周期跑 onBackgroundTick（0 = 等价 suspend）
                      own_task = 2,  ///< 申请自己的 FreeRTOS 任务（栈深/优先级也在这里配）
                  });

/// 后台武装策略（issue 24 / ADR 0010）：声明后台**从什么时候开始跑**。
/// 默认 on_first_enter = 被用户打开过一次才武装（issue 23 / ADR 0009）；
/// at_boot = 开机装配时就武装（闹钟这类"一上电就要工作"的 App 才用）。
E_FMT_DERIVE_ENUM(enum class ArmPolicy
                  : std::uint8_t{
                      on_first_enter = 0,  ///< 第一次进过前台才武装（默认）
                      at_boot = 1,         ///< boot 装配时就武装（不管有没有被打开过）
                  });

/// 每 App 的后台配置（App::settings() 返回；默认 = 纯前台、后台不跑）。
E_FMT_DERIVE(struct AppSettings {
  BackgroundPolicy background = BackgroundPolicy::suspend;
  std::uint32_t period_ms = 0;         ///< tick 策略的周期；0 等价 suspend
  std::uint16_t task_stack_words = 0;  ///< own_task 策略的栈深（单位：StackType_t 字）
  std::uint16_t task_priority = 0;  ///< own_task 策略的优先级（2 字节，见 message.h 的说明）
  ArmPolicy arm = ArmPolicy::on_first_enter;  ///< 何时武装（见上；4 字段聚合初始化照旧可用）
});

/// App 基类 —— 钩子集合 = spec §5 + onForegroundTick（issue 19 / ADR 0008）。
class App {
 public:
  App() noexcept = default;
  virtual ~App() = default;

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /// 应用名（日志/注册表查找用；要求唯一、非空）。
  [[nodiscard]] virtual const char* name() const = 0;

  /// 后台配置（spec §5：per-App 策略，默认 Suspend）。
  [[nodiscard]] virtual AppSettings settings() const { return AppSettings{}; }

// --- 展示元数据（issue 16；零堆、编译期常量）----------------------------------
  // 元数据走虚函数，而不是在注册表里再挂第二张表：注册表的表项仍是 App*
  // （app_registry.h 的宏形状一行不改），"表长 = App 数"由 App 表本身的
// static_assert 保证。元数据由框架的扩展点读取，框架自身不解释它们。

/// 展示标题（启动器槽位等用途）。默认取 name()：任何 App 都有可读标题；
  /// 中文标题由 App 自己覆写（进静态子集字库，见 issue 17 的缺字审计）。
  [[nodiscard]] virtual const char* title() const { return name(); }

  /// 槽位图标：LV_SYMBOL_* 码点字符串（例如 LV_SYMBOL_HOME）。
  /// 空 = 启动器用标题首字符兜底（此时该字符必须在静态子集字库里）。
  [[nodiscard]] virtual const char* icon() const { return nullptr; }

  /// 主题色（0xRRGGBB）：取 <embark/design_tokens.h> 的令牌，禁止魔法颜色。
  [[nodiscard]] virtual std::uint32_t accent() const { return design_tokens::accent; }

  /// 装配期，进程生命周期里恰好一次。HAL 能力从这里注入（fw.hal()）。
  virtual void onCreate(Framework& fw) = 0;

  /// 第一次成为前台。
  virtual void onEnter() = 0;

  /// 离开前台。框架只通知，不碰 App 的 UI。
  virtual void onPause() = 0;

  /// 再次成为前台（onEnter 只发一次，之后都是 onResume）。
  virtual void onResume() = 0;

  /// 后台节拍（tick 策略下按 settings().period_ms 周期被调）。必须轻量：
  /// 它在唯一 UI 任务的循环里执行（spec §6），阻塞/长耗时工作必须走 own_task。
  virtual void onBackgroundTick(std::uint32_t now_ms) { (void)now_ms; }

  /// 前台节拍（issue 19 / ADR 0008，EEZ Studio 适配引入）：当前台 App 每帧一次，
  /// 在 step 第 4 段 lv_timer_handler 之后调用，专供 EEZ App 驱动 eez_flow_tick()。
  /// 默认空实现——手写 App 不需要覆写；前台专属，离开前台即停。
  virtual void onForegroundTick(std::uint32_t now_ms) { (void)now_ms; }

  /// 框架投递的消息（issue 07 起有真正的投递路径；v1 期间 App 之间不直接通信）。
  virtual void onMessage(const Message& msg) { (void)msg; }

  /// 退出通知（v1 只在关机路径触发；最后给每个 App 一次收尾机会）。
  virtual void onExit() = 0;
};

}  // namespace embark

#endif /* EMBARK_APP_H */