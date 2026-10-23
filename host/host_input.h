/**
 * 宿主 · 输入后端（SDL2，issues/05）
 *
 * 鼠标 → move/press/release，键盘 → press/release（key = SDL scancode 的低字节），
 * 时间戳走 HAL 时间轴（hal::ITime::now_ms）。坐标用
 * SDL_RenderWindowToLogical() 从窗口像素换算到 240×320 的逻辑坐标 —— 窗口被放大、
 * 甚至被改成其它比例时都不用在这里操心换算规则（那正是 setLogicalSize 的作用）。
 *
 * 为什么"退出"也在这里：SDL 只有一个事件队列，泵事件的职责在 poll()。窗口关闭
 * （SDL_QUIT 或点窗口 X）不是输入事件，但它是队列里的事件，捎带记下来最省事 ——
 * UI 循环每帧 poll 到 false 之后看一次 quit_requested() 即可（poll 必须被抽干，
 * 否则队列里排在退出事件后面的输入事件会把退出信号挡住）。
 */
#ifndef EMBARK_PLATFORM_HOST_INPUT_H
#define EMBARK_PLATFORM_HOST_INPUT_H

#include <cstddef>
#include <cstdint>

#include <SDL.h>

#include <embark/error.h>
#include <embark/hal/input.h>
#include <embark/hal/time.h>
#include <middleware/etl/expected.h>

#include "host_display.h"

namespace embark::platform::host {

class HostInput final : public hal::IInput {
 public:
  HostInput(hal::ITime& time, HostDisplay& display) noexcept;

  HostInput(const HostInput&) = delete;
  HostInput& operator=(const HostInput&) = delete;

  /// 必须在 HostDisplay::init() 之后调用：输入读的是同一个视频子系统的窗口事件。
  [[nodiscard]] Error init() noexcept override;

  [[nodiscard]] bool is_ready() const noexcept { return ready_; }

  [[nodiscard]] etl::expected<bool, Error> poll(hal::InputEvent& event) noexcept override;

  /// 窗口是否已经被要求关闭（SDL_QUIT / 窗口 X / Alt+F4）。
  [[nodiscard]] bool quit_requested() const noexcept { return quit_requested_; }

  /// 清掉退出请求（演示程序在"跑够帧数"与"用户关窗"之间做区分时用得上）。
  void clear_quit_request() noexcept { quit_requested_ = false; }

  /// 已经丢弃的事件数（SDL 里排不进 HAL 事件模型的那些，例如 scancode > 255）。
  [[nodiscard]] std::size_t dropped_events() const noexcept { return dropped_events_; }

 private:
  hal::ITime& time_;
  HostDisplay& display_;
  bool ready_ = false;
  bool quit_requested_ = false;
  std::size_t dropped_events_ = 0;
  std::uint16_t last_x_ = 0;  ///< 键盘事件没有坐标：带上最近一次鼠标位置，LVGL 的指针才不乱跳
  std::uint16_t last_y_ = 0;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_INPUT_H */
