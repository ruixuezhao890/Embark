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

  // 总线先起：2026-10-17 起时间后端要借它读板载 RTC（I2C_NUM_1，IMU 与 RTC 共用）。
  // 建总线本身不发任何事务，失败只可能是引脚/控制器被占 —— 放在时间之前，"总线没起来"
  // 与"RTC 读不到"才分得开。
  const Error bus_error = bus_.init();
  if (bus_error != Error::none) {
    return bus_error;
  }

  // 时间：now_ms/delay_ms 本来不需要初始化，这一步实际做的是"探 RTC 并把墙钟锚在本刻"。
  // 传 &bus_ 而不是让它自己建总线：同一块板只该有一个 I2C_NUM_1 的主人（句柄也是复用的）。
  const Error time_error = time_.init(&bus_);
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

  // SD 卡放最后（issue 25）：挂载要等卡上电、跑 SPI 协商，失败原因还可能是"根本没插卡"——
  // 那是正常状态而不是故障，所以只记日志、不返回错误，也不接 Context::filesystem。
  const Error sd_error = sd_.mount();
  if (sd_error == Error::none) {
    const Error root_error = filesystem_.attach_root(sd_.mount_point());
    if (root_error == Error::none) {
      context_.filesystem = &filesystem_;
    } else {
      ELOG_WARN("SD：挂载成功但文件系统根目录装不上（错误码 {}），运行时字库不可用",
                static_cast<int>(root_error));
    }
  }

  initialized_ = true;
  return Error::none;
}

}  // namespace embark::platform::esp32
