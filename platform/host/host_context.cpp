#include "host_context.h"

#include <middleware/elog/elog.hpp>

namespace embark::platform::host {
namespace {

constexpr const char* kLoggerName = "embark";

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
    const e_log::logger* const logger = install_logger(kLoggerName, binder_);
    log_installed_ = (logger != nullptr);
  }
  return storage_.init();
}

}  // namespace embark::platform::host
