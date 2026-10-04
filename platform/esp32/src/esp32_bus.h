/**
 * Embark ESP32-S3 · 总线后端（issues/11）
 *
 * 这条 `hal::IBus` 指的是**板载 I2C**（IO11/IO10，与 IMU + RTC 共用），不是屏幕那条 SPI：
 *   - 屏幕像素由 esp_lcd 直接驱动（它自己持 SPI 总线、自己走 DMA），框架侧不需要再开一路；
 *   - 触摸有自己独立的 I2C 总线（IO1/IO3），由 esp32_input.cpp 自己管，也不走这里。
 *
 * 所以 spi_transfer() 在真机上如实返回 unsupported —— 等真要挂 SD 卡之类的外设时，
 * 再在这里加一条 SPI 会话（那时它会有明确的主人）。
 *
 * 句柄懒创建：HAL 的接口只给 7 位地址，不给"注册设备"这一步，所以第一次用到某个地址时
 * 才在总线上去挂它，之后复用（最多 bus_max_devices 个）。
 */
#ifndef EMBARK_PLATFORM_ESP32_BUS_H
#define EMBARK_PLATFORM_ESP32_BUS_H

#include <cstddef>
#include <cstdint>

#include <embark/hal/bus.h>

#include <driver/i2c_master.h>
#include <etl/array.h>

#include "esp32_board.h"

namespace embark::platform::esp32 {

class Esp32Bus final : public hal::IBus {
 public:
  Esp32Bus() noexcept = default;
  Esp32Bus(const Esp32Bus&) = delete;
  Esp32Bus& operator=(const Esp32Bus&) = delete;
  ~Esp32Bus() override;

  /// 建 I2C 主机总线（`esp32_board.h` 的 `bus_i2c_port`）。幂等。
  [[nodiscard]] Error init() noexcept;

  Error i2c_write(std::uint8_t address7, etl::span<const std::uint8_t> data) noexcept override;
  Error i2c_write_read(std::uint8_t address7, etl::span<const std::uint8_t> write_data,
                       etl::span<std::uint8_t> read_data) noexcept override;
  Error spi_transfer(etl::span<const std::uint8_t> out,
                     etl::span<std::uint8_t> in) noexcept override;

 private:
  /// 取（必要时创建）某个从设备地址的句柄。
  [[nodiscard]] Error device_for(std::uint8_t address7, i2c_master_dev_handle_t* out) noexcept;

  struct DeviceSlot {
    std::uint8_t address = 0;
    i2c_master_dev_handle_t handle = nullptr;
  };

  i2c_master_bus_handle_t bus_ = nullptr;
  /// 总线是我们建的吗？沿用了别人建的总线时，析构里不能拆（拆了会把别的外设一起带走）。
  bool bus_owned_ = false;
  etl::array<DeviceSlot, bus_max_devices> devices_{};
  std::size_t device_count_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_BUS_H */
