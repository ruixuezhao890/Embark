/**
 * @file esp32_hal.h
 * @brief 真机 · HAL 装配（issue 11）
 *
 * 与宿主（platform/host/host_context.h）同构：全固件只有一份 HAL，Context 用引用而不是
 * 指针，装配点只有这里一处；实例放函数内静态，绕开静态初始化顺序问题。
 *
 * 唯一的差异：宿主的外设（显示/输入）由入口按需 attach 进来，真机的显示与输入就是板载
 * ST7789 + CST328，直接是 Esp32Hal 的成员 —— 少一次装配，也少一处可能忘记挂的地方。
 *
 * 初始化顺序：日志最先（后面每一步失败都要有出口），然后时间/系统，再存储与总线，
 * 最后显示与输入（要动 GPIO / SPI / I2C，放在能打日志之后出问题才看得见）。
 */
#ifndef EMBARK_PLATFORM_ESP32_HAL_H
#define EMBARK_PLATFORM_ESP32_HAL_H

#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/log.h>

#include "esp32_bus.h"
#include "esp32_display.h"
#include "esp32_input.h"
#include "esp32_log_sink.h"
#include "esp32_persistence.h"
#include "esp32_system.h"
#include "esp32_time.h"

namespace embark::platform::esp32 {

class Esp32Hal {
 public:
  static Esp32Hal& instance() noexcept;

  Esp32Hal(const Esp32Hal&) = delete;
  Esp32Hal& operator=(const Esp32Hal&) = delete;

  /// 依次初始化各后端并装好默认 logger。返回第一个失败项；none = 全部就绪；幂等。
  /// 顺序：log → time → system → storage → bus → display → input。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] hal::Context& context() noexcept { return context_; }

  /// HAL 时间轴：UI 循环的节拍、输入事件时间戳、LVGL 的时钟都走它。
  [[nodiscard]] Esp32Time& time() noexcept { return time_; }
  [[nodiscard]] Esp32System& system() noexcept { return system_; }
  [[nodiscard]] Esp32Persistence& storage() noexcept { return storage_; }
  [[nodiscard]] Esp32Bus& bus() noexcept { return bus_; }
  [[nodiscard]] Esp32Display& display() noexcept { return display_; }
  [[nodiscard]] Esp32Input& input() noexcept { return input_; }

 private:
  Esp32Hal() noexcept
      : binder_(log_), context_{time_, storage_, log_, system_, bus_, &display_, &input_} {}

  Esp32Time time_;
  Esp32Persistence storage_;
  Esp32LogSink log_;
  Esp32System system_;
  Esp32Bus bus_;
  Esp32Display display_;
  Esp32Input input_;
  LogSinkBinder binder_;  // 必须活在 logger 之后：elog 里存的是它的指针
  hal::Context context_;

  bool log_installed_ = false;
  bool initialized_ = false;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_HAL_H */
