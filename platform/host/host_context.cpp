#include "host_context.h"

#include <middleware/elog/elog.hpp>

namespace embark::platform::host {
namespace {

constexpr const char* logger_name = "embark";

}  // namespace

HostHal& HostHal::instance() noexcept {
  static HostHal hal;
  return hal;
}

Error HostHal::init() noexcept {
  if (!log_.init()) {
    return Error::io_failure;  // 日志后端起不来：后面每一步都没出口了
  }
  if (!log_installed_) {
    const e_log::logger* const logger = install_logger(logger_name, binder_);
    log_installed_ = (logger != nullptr);
  }

  const Error storage_error = storage_.init();
  if (storage_error != Error::none) {
    return storage_error;
  }

  // 显示与输入是可选的（issues/05）：没挂就没有，v1 允许无屏平台先跑逻辑。
  if (context_.display != nullptr) {
    const Error display_error = context_.display->init();
    if (display_error != Error::none) {
      return display_error;
    }
  }
  if (context_.input != nullptr) {
    if (context_.display == nullptr || !context_.display->is_ready()) {
      return Error::not_ready;  // 宿主输入后端要借渲染器换算坐标，没有显示就没有输入
    }
    const Error input_error = context_.input->init();
    if (input_error != Error::none) {
      return input_error;
    }
  }
  return Error::none;
}

}  // namespace embark::platform::host
