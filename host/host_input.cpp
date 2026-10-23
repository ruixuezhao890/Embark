#include "host_input.h"

#include <cmath>

#include <embark/error.h>
#include <embark_limits.h>
#include <middleware/elog/elog.hpp>

namespace embark::platform::host {
namespace {

/// 窗口像素 → 逻辑坐标：四舍五入后夹到面板范围内（LVGL 不接受越界坐标）。
std::uint16_t to_panel_coordinate(float value, std::size_t limit) noexcept {
  if (!(value > 0.0F)) {  // 顺带挡掉 NaN
    return 0;
  }
  const float rounded = std::round(value);
  if (rounded >= static_cast<float>(limit)) {
    return static_cast<std::uint16_t>(limit - 1U);
  }
  return static_cast<std::uint16_t>(rounded);
}

/// HAL 的 key 用键号（v1 约定 0..255），这里取 scancode 低字节；超出的（例如多媒体键）丢弃并计数。
bool to_panel_key(SDL_Scancode scancode, std::uint16_t& key) noexcept {
  if (scancode < 0 || scancode > 255) {
    return false;
  }
  key = static_cast<std::uint16_t>(scancode);
  return true;
}

}  // namespace

HostInput::HostInput(hal::ITime& time, HostDisplay& display) noexcept
    : time_(time), display_(display) {}

Error HostInput::init() noexcept {
  if (display_.renderer() == nullptr) {
    return Error::not_ready;  // 显示后端没起来：事件队列还不存在
  }
  ready_ = true;
  return Error::none;
}

etl::expected<bool, Error> HostInput::poll(hal::InputEvent& event) noexcept {
  if (!ready_) {
    return unexpected(Error::not_ready);
  }

  SDL_Event raw{};
  while (SDL_PollEvent(&raw) != 0) {
    switch (raw.type) {
      case SDL_QUIT:
        quit_requested_ = true;
        continue;

      case SDL_WINDOWEVENT:
        if (raw.window.event == SDL_WINDOWEVENT_CLOSE) {
          quit_requested_ = true;
        }
        continue;

      case SDL_MOUSEMOTION:
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        float logical_x = 0.0F;
        float logical_y = 0.0F;
        int window_x = 0;
        int window_y = 0;

        if (raw.type == SDL_MOUSEMOTION) {
          window_x = raw.motion.x;
          window_y = raw.motion.y;
        } else {
          window_x = raw.button.x;
          window_y = raw.button.y;
        }
        SDL_RenderWindowToLogical(display_.renderer(), window_x, window_y, &logical_x, &logical_y);
        last_x_ = to_panel_coordinate(logical_x, display_width);
        last_y_ = to_panel_coordinate(logical_y, display_height);

        if (raw.type == SDL_MOUSEMOTION) {
          event = hal::InputEvent{hal::InputEventKind::move, last_x_, last_y_, 0, time_.now_ms()};
        } else {
          // key 一律 0：HAL 约定"指针事件的 key 恒为 0，按键事件才带非 0 键号"（hal/types.h）。
          // 鼠标的 button 号（1 = 左键）v1 用不上；把它塞进 key 会让上层把点击当成键盘按键整条丢掉
          // —— 第一轮验收就是这么失败的（合成点击被 lvgl_port 计进"忽略按键"）。
          event = hal::InputEvent{raw.type == SDL_MOUSEBUTTONDOWN ? hal::InputEventKind::press
                                                                  : hal::InputEventKind::release,
                                  last_x_, last_y_, 0, time_.now_ms()};
        }
        return true;
      }

      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        std::uint16_t key = 0;
        if (!to_panel_key(raw.key.keysym.scancode, key)) {
          ++dropped_events_;
          continue;
        }
        event = hal::InputEvent{
            raw.type == SDL_KEYDOWN ? hal::InputEventKind::press : hal::InputEventKind::release,
            last_x_, last_y_, key, time_.now_ms()};
        // 整对象打（issue 13）：键盘事件只有人按才来（合成点击是鼠标，走上面那条），
        // 不会刷屏；这里顺带演示 InputEvent 的整对象输出（含嵌套的 InputEventKind）。
        ELOG_INFO("键盘事件 {}", event);
        return true;
      }

      default:
        continue;  // 其它事件（焦点、文本输入…）v1 不建模
    }
  }

  return false;  // 队列抽干了：现在没有事件（不是错误）
}

}  // namespace embark::platform::host
