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
 * 钩子集合与 spec §5 完全一致（实现可在细节上微调，但钩子集合不再增加）。
 */
#ifndef EMBARK_APP_H
#define EMBARK_APP_H

#include <cstdint>

#include <embark/message.h>

namespace embark {

class Framework;

// AppId / invalid_app_id 定义在 <embark/message.h>（消息投递也用它们），见下。

/// 后台策略（spec §5、§6）：每 App 一个配置，App 自己声明自己是哪种后台。
enum class BackgroundPolicy : std::uint8_t {
  suspend = 0,   ///< 完全不跑（默认）
  tick = 1,      ///< 按 period_ms 周期跑 onBackgroundTick（0 = 等价 suspend）
  own_task = 2,  ///< 申请自己的 FreeRTOS 任务（栈深/优先级也在这里配）
};

/// 每 App 的后台配置（App::settings() 返回；默认 = 纯前台、后台不跑）。
struct AppSettings {
  BackgroundPolicy background = BackgroundPolicy::suspend;
  std::uint32_t period_ms = 0;         ///< tick 策略的周期；0 等价 suspend
  std::uint16_t task_stack_words = 0;  ///< own_task 策略的栈深（单位：StackType_t 字）
  std::uint8_t task_priority = 0;      ///< own_task 策略的优先级
};

/// App 基类 —— 钩子集合与 spec §5 一致，不再增加。
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

  /// 框架投递的消息（issue 07 起有真正的投递路径；v1 期间 App 之间不直接通信）。
  virtual void onMessage(const Message& msg) { (void)msg; }

  /// 退出通知（v1 只在关机路径触发；最后给每个 App 一次收尾机会）。
  virtual void onExit() = 0;
};

}  // namespace embark

#endif /* EMBARK_APP_H */