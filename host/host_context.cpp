#include "host_context.h"

#include <embark/hal/audio.h>
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

  // 文件系统后端（issue 17）：根目录定下来就可以用；宿主路径自持，不依赖 LVGL 的 FS 驱动。
  const Error filesystem_error = filesystem_.init();
  if (filesystem_error != Error::none) {
    return filesystem_error;
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

  // 音频服务（issue 09）：跟显示/输入同一条口径 —— 挂了就初始化，没挂就跳过。
  // **失败不致命**：没有声卡的机器照样要能跑 UI（与 IAudio 头注释里那条一致），
  // 只记一条 WARN，剩下的交给调用方（--audio-test 会自己报「本机没有音频输出」）。
  if (context_.audio != nullptr) {
    const Error audio_error = context_.audio->init();
    if (audio_error != Error::none) {
      ELOG_WARN("音频：后端初始化失败（{}）—— 没有音频输出的机器照样继续跑", audio_error);
    }
  }

  // 网络服务（issue 10）：socket 栈是进程级的。**失败不致命** —— 没网的机器照样要能跑 UI
  // （与 IAudio 同一条口径）；装进 Context 后 connect/http_get 自己会报 not_ready。
  context_.network = &network_;
  const Error network_error = network_.init();
  if (network_error != Error::none) {
    ELOG_WARN("网络：后端初始化失败（{}）—— 没有网络的机器照样继续跑", network_error);
  }

  // 姿态服务（issue 08）：宿主这份不需要外部资源，init 只记时间原点，永远成功。
  // 仍然照契约调一次 —— 让「init 幂等」这件事在宿主上也真的走一遍，而不是被跳过。
  const Error imu_error = imu_.init();
  if (imu_error != Error::none) {
    return imu_error;
  }
  return Error::none;
}

}  // namespace embark::platform::host
