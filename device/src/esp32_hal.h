/**
 * @file esp32_hal.h
 * @brief 真机 · HAL 装配（issue 11）
 *
 * 与宿主（host/host_context.h）同构：全固件只有一份 HAL，Context 用引用而不是
 * 指针，装配点只有这里一处；实例放函数内静态，绕开静态初始化顺序问题。
 *
 * 唯一的差异：宿主的外设（显示/输入）由入口按需 attach 进来，真机的显示与输入就是板载
 * ST7789 + CST328，直接是 Esp32Hal 的成员 —— 少一次装配，也少一处可能忘记挂的地方。
 *
 * 初始化顺序：日志最先（后面每一步失败都要有出口），然后总线与时间（时间后端要借板载
 * I2C 读 RTC，所以总线必须在它之前；2026-10-17 起这个次序比"时间/系统再总线"要紧），
 * 再系统与存储，接着显示与输入（要动 GPIO / SPI / I2C，放在能打日志之后出问题才看得见），
 * 最后挂 SD 卡（issue 25：插卡是可选路径，挂在最后，失败只记日志、不挡启动）。
 */
#ifndef EMBARK_PLATFORM_ESP32_HAL_H
#define EMBARK_PLATFORM_ESP32_HAL_H

#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/log.h>

#include "esp32_audio.h"
#include "esp32_bus.h"
#include "esp32_display.h"
#include "esp32_filesystem.h"
#include "esp32_imu.h"
#include "esp32_input.h"
#include "esp32_log_sink.h"
#include "esp32_network.h"
#include "esp32_persistence.h"
#include "esp32_sd.h"
#include "esp32_system.h"
#include "esp32_time.h"

namespace embark::platform::esp32 {

class Esp32Hal {
 public:
  static Esp32Hal& instance() noexcept;

  Esp32Hal(const Esp32Hal&) = delete;
  Esp32Hal& operator=(const Esp32Hal&) = delete;

  /// 依次初始化各后端并装好默认 logger。返回第一个失败项；none = 全部就绪；幂等。
  /// 顺序：log → bus → time → system → storage → display → input → sd。
  /// （bus 在 time 之前：时间后端要用板载 I2C 读 PCF85063，见 time_.init(&bus_)。）
  /// SD 挂载失败（没配置引脚 / 没插卡 / 认不出卡）不算失败：只记日志，Context::filesystem
  /// 保持 nullptr，运行时字库服务据此返回 not_ready。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] hal::Context& context() noexcept { return context_; }

  /// HAL 时间轴：UI 循环的节拍、输入事件时间戳、LVGL 的时钟都走它。
  [[nodiscard]] Esp32Time& time() noexcept { return time_; }
  [[nodiscard]] Esp32System& system() noexcept { return system_; }
  [[nodiscard]] Esp32Persistence& storage() noexcept { return storage_; }
  [[nodiscard]] Esp32Bus& bus() noexcept { return bus_; }
  [[nodiscard]] Esp32Display& display() noexcept { return display_; }
  [[nodiscard]] Esp32Input& input() noexcept { return input_; }
  /// SD 卡（issue 25）：没插卡时 mounted() == false，其余接口照样能问。
  [[nodiscard]] SdCard& sd() noexcept { return sd_; }
  [[nodiscard]] Esp32FileSystem& filesystem() noexcept { return filesystem_; }
  /// 姿态服务（issue 08）：没有这颗芯片时 present() == false，read() 返回 not_ready。
  [[nodiscard]] Esp32Imu& imu() noexcept { return imu_; }
  /// 音频服务（issue 09）：I2S 通道没建起来时 channel_ready() == false。
  [[nodiscard]] Esp32Audio& audio() noexcept { return audio_; }
  /// 网络服务（issue 10）：协议栈没起来时 ready() == false，所有操作返回 not_ready。
  [[nodiscard]] Esp32Network& network() noexcept { return network_; }

 private:
  Esp32Hal() noexcept
      // filesystem 先留 nullptr：它指的是 SD 挂载点上的一块盘（issue 25），只有 init() 里
      // 挂载成功才接上 —— 没卡的机器这格永远是空，运行时字库服务据此返回 not_ready。
      : binder_(log_), context_{time_,   storage_, log_,  system_, bus_,   &display_,
                                &input_, nullptr,  &imu_, &audio_, &network_} {}

  Esp32Time time_;
  Esp32Persistence storage_;
  Esp32LogSink log_;
  Esp32System system_;
  Esp32Bus bus_;
  Esp32Display display_;
  Esp32Input input_;
  SdCard sd_;  // SD 卡（issue 25）：没插卡也活着，只是 mounted() == false
  Esp32FileSystem filesystem_;  // 根 = SD 挂载点；只在挂载成功后才被 Context 指到
  Esp32Imu imu_;  // 板载六轴（issue 08）：没焊/没应答时 present() == false
  Esp32Audio audio_;  // 板载 I2S 音频（issue 09）：通道建不起来时 channel_ready() == false
  Esp32Network network_;  // WiFi STA + HTTP + SNTP（issue 10）：协议栈起不来时 ready() == false
  LogSinkBinder binder_;  // 必须活在 logger 之后：elog 里存的是它的指针
  hal::Context context_;

  bool log_installed_ = false;
  bool initialized_ = false;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_HAL_H */
