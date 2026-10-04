#include "esp32_hal.h"

#include <middleware/elog/elog.hpp>

namespace embark::platform::esp32 {
namespace {

constexpr const char* logger_name = "embark";

}  // namespace

Esp32Hal& Esp32Hal::instance() noexcept {
  static Esp32Hal hal;
  return hal;
}

Error Esp32Hal::init() noexcept {
  if (initialized_) {
    return Error::none;  // 幂等：app_main 只调一次，重复调不该出错
  }

  if (!log_.init()) {
    return Error::io_failure;  // 日志后端起不来：后面每一步都没出口了
  }
  if (!log_installed_) {
    const e_log::logger* const logger = install_logger(logger_name, binder_);
    log_installed_ = (logger != nullptr);
  }

  const Error time_error = time_.init();
  if (time_error != Error::none) {
    return time_error;
  }
  const Error system_error = system_.init();
  if (system_error != Error::none) {
    return system_error;
  }
  const Error storage_error = storage_.init();
  if (storage_error != Error::none) {
    return storage_error;
  }
  const Error bus_error = bus_.init();
  if (bus_error != Error::none) {
    return bus_error;
  }

  // 显示与输入：真机上都在（板载屏与触摸），但还是按 Context 的可空语义判空 ——
  // 谁哪天做一块无屏板子，把成员指针摘掉就能跑逻辑。
  if (context_.display != nullptr) {
    const Error display_error = context_.display->init();
    if (display_error != Error::none) {
      return display_error;
    }
  }
  if (context_.input != nullptr) {
    // 与宿主的差异：宿主输入要借渲染器换算坐标，真机触摸是独立 I2C 设备，不依赖显示。
    const Error input_error = context_.input->init();
    if (input_error != Error::none) {
      return input_error;
    }
  }

  initialized_ = true;
  return Error::none;
}

}  // namespace embark::platform::esp32
