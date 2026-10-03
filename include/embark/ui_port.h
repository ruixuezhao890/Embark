/**
 * Embark · UI 端口（issues/06）
 *
 * spec §6：全局唯一 UI 任务承载"输入事件处理 → 前台 App 回调 → lv_timer_handler()
 * → 后台 tick 调度 → 消息派发"，它是全工程唯一允许操作 LVGL 与调用
 * lv_timer_handler() 的地方。Framework 不直接认识 LVGL —— 它只认识这个端口：
 *
 *   - init()         进入循环前装配（LVGL 初始化、驱动注册；失败 → boot 失败）；
 *   - tick()         推进 LVGL 时间轴（必须每帧一次、在 pump_input 之前）；
 *   - pump_input()   把 HAL 输入队列抽干（输入事件的消费是 UI 循环的职责）；
 *   - process()      界面工作（宿主 = lv_timer_handler()：定时器、重绘、输入事件
 *                    回调全在这里发生，前台 App 的点击回调就是从这里面蹦出来的）；
 *   - exit_requested()  UI 层是否请求退出（宿主 = 用户关窗）；
 *   - shutdown()     退出路径收尾。
 *
 * 端口实现见 platform/host/lvgl_ui_port.{h,cpp}（宿主）；真机端口在 issue 11。
 * 测试用 tests/fakes/fake_ui_port.h。
 *
 * 线程纪律：本端口所有方法只被唯一 UI 任务调用（framework 保证），实现里不需要锁。
 */
#ifndef EMBARK_UI_PORT_H
#define EMBARK_UI_PORT_H

#include <embark/error.h>

namespace embark {

class IUiPort {
 public:
  virtual ~IUiPort() = default;

  IUiPort() noexcept = default;  // 显式保留默认构造（拷贝被删后隐式的会被吞掉）
  IUiPort(const IUiPort&) = delete;
  IUiPort& operator=(const IUiPort&) = delete;

  /// 装配 UI 层（LVGL 初始化/驱动注册）。幂等；失败返回具体 Error。
  [[nodiscard]] virtual Error init() = 0;

  /// 推进时间轴。每帧一次、必须先于 pump_input/process 调用。
  virtual void tick() noexcept = 0;

  /// 抽干 HAL 输入队列（事件消费属于 UI 循环，不是 LVGL 内部职责）。
  virtual void pump_input() noexcept = 0;

  /// 界面工作：定时器、重绘、输入派发（宿主 = lv_timer_handler()）。
  virtual void process() noexcept = 0;

  /// UI 层是否请求退出（宿主 = 关窗；真机 = 按电源键等，issue 11 定）。
  [[nodiscard]] virtual bool exit_requested() const noexcept = 0;

  /// 退出路径收尾（框架 shutdown 时调用；按需实现，不做强制语义）。
  virtual void shutdown() noexcept = 0;
};

}  // namespace embark

#endif /* EMBARK_UI_PORT_H */