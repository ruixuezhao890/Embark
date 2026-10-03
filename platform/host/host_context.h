/**
 * 宿主 · HAL 装配（issues/04）
 *
 * 全进程只有一份 HAL —— 这也是 Context 用引用而不是指针的原因：装配点只有这里一处。
 * 实例放在函数内静态（instance() 的局部静态），绕开静态初始化顺序问题：
 * 谁先用到谁触发构造，构造完就一直活着。
 *
 * 初始化顺序有讲究：日志最先（后面每一步失败都要有出口），然后才是存储。
 */
#ifndef EMBARK_PLATFORM_HOST_CONTEXT_H
#define EMBARK_PLATFORM_HOST_CONTEXT_H

#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/log.h>

#include "host_bus.h"
#include "host_log_sink.h"
#include "host_persistence.h"
#include "host_system.h"
#include "host_time.h"

namespace embark::platform::host {

class HostHal {
 public:
  static HostHal& instance() noexcept;

  HostHal(const HostHal&) = delete;
  HostHal& operator=(const HostHal&) = delete;

  /// 依次初始化各后端并装好默认 logger。返回第一个失败项；none = 全部就绪。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] hal::Context& context() noexcept { return context_; }

  [[nodiscard]] const char* storage_path() const noexcept { return storage_.path(); }

 private:
  HostHal() noexcept
      : binder_(log_), context_{time_, storage_, log_, system_, bus_, nullptr, nullptr} {}

  HostTime time_;
  HostPersistence storage_;
  HostLogSink log_;
  HostSystem system_;
  HostBus bus_;
  LogSinkBinder binder_;  // 必须活在 logger 之后：elog 里存的是它的指针
  hal::Context context_;

  bool log_installed_ = false;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_CONTEXT_H */
