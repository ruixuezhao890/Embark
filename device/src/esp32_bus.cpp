/**
 * Embark ESP32-S3 · 总线后端实现（issues/11）
 */
#include "esp32_bus.h"

#include <embark/error.h>
#include <middleware/elog/elog.hpp>

#include <esp_err.h>

namespace embark::platform::esp32 {

Esp32Bus::~Esp32Bus() {
  for (std::size_t index = 0; index < device_count_; ++index) {
    if (devices_[index].handle != nullptr) {
      (void)i2c_master_bus_rm_device(devices_[index].handle);
      devices_[index].handle = nullptr;
    }
  }
  device_count_ = 0;
  if (bus_ != nullptr) {
    if (bus_owned_) {
      (void)i2c_del_master_bus(bus_);  // 别人建的总线不归我们拆
      bus_owned_ = false;
    }
    bus_ = nullptr;
  }
}

Error Esp32Bus::init() noexcept {
  if (bus_ != nullptr) {
    return Error::none;  // 幂等
  }

  // 逐字段赋值而不是指定初始化器：C++17 下后者是编译器扩展（-Wpedantic 会叫）。
  i2c_master_bus_config_t config = {};
  config.i2c_port = static_cast<i2c_port_num_t>(bus_i2c_port);
  // 引脚常量在 esp32_board.h 里是 int（那个头不引 IDF 头）；IDF 要 gpio_num_t，显式转。
  config.sda_io_num = static_cast<gpio_num_t>(bus_pin_sda);
  config.scl_io_num = static_cast<gpio_num_t>(bus_pin_scl);
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;

  const esp_err_t error = i2c_new_master_bus(&config, &bus_);
  if (error == ESP_ERR_INVALID_STATE) {
    // 这个控制器已经被别处建过（同一个固件里只该有一个主人）：取回现成句柄，不当错误。
    const esp_err_t found =
        i2c_master_get_bus_handle(static_cast<i2c_port_num_t>(bus_i2c_port), &bus_);
    if (found != ESP_OK) {
      ELOG_ERROR("I2C{} 已初始化但取不到句柄：{}", bus_i2c_port, esp_err_to_name(found));
      bus_ = nullptr;
      return Error::io_failure;
    }
    ELOG_WARN("板载 I2C：I2C{} 上已有总线，沿用现有句柄", bus_i2c_port);
    return Error::none;
  }
  if (error != ESP_OK) {
    ELOG_ERROR("i2c_new_master_bus(I2C{}, SDA {} / SCL {}) 失败：{}", bus_i2c_port, bus_pin_sda,
               bus_pin_scl, esp_err_to_name(error));
    bus_ = nullptr;
    return Error::io_failure;
  }
  bus_owned_ = true;
  return Error::none;
}

Error Esp32Bus::device_for(std::uint8_t address7, i2c_master_dev_handle_t* out) noexcept {
  if (bus_ == nullptr) {
    return Error::not_ready;
  }
  if (address7 > 0x7FU) {
    return Error::invalid_argument;  // 7 位地址不可能到 0x80 以上
  }
  if (out == nullptr) {
    return Error::invalid_argument;
  }

  for (std::size_t index = 0; index < device_count_; ++index) {
    if (devices_[index].address == address7) {
      *out = devices_[index].handle;
      return Error::none;
    }
  }

  if (device_count_ >= bus_max_devices) {
    // 不驱逐已有句柄：那会让正在用它的调用方拿到野指针。如实报满。
    ELOG_WARN("总线设备句柄已满（{} 个），地址 0x{:02X} 无法挂载",
              static_cast<unsigned>(bus_max_devices), static_cast<unsigned>(address7));
    return Error::no_space;
  }

  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address7;
  config.scl_speed_hz = bus_i2c_hz;

  i2c_master_dev_handle_t handle = nullptr;
  const esp_err_t error = i2c_master_bus_add_device(bus_, &config, &handle);
  if (error != ESP_OK) {
    ELOG_WARN("i2c_master_bus_add_device(0x{:02X}) 失败：{}", static_cast<unsigned>(address7),
              esp_err_to_name(error));
    return Error::io_failure;
  }

  devices_[device_count_].address = address7;
  devices_[device_count_].handle = handle;
  ++device_count_;
  *out = handle;
  return Error::none;
}

Error Esp32Bus::i2c_write(std::uint8_t address7, etl::span<const std::uint8_t> data) noexcept {
  if (data.empty() || data.data() == nullptr) {
    return Error::invalid_argument;
  }

  i2c_master_dev_handle_t device = nullptr;
  const Error error = device_for(address7, &device);
  if (error != Error::none) {
    return error;
  }

  const esp_err_t result =
      i2c_master_transmit(device, data.data(), data.size(), static_cast<int>(bus_i2c_timeout_ms));
  if (result == ESP_ERR_TIMEOUT) {
    return Error::timeout;
  }
  // 设备没应答（NACK）也是走这里 —— HAL 的契约把它归到 io_failure。
  return result == ESP_OK ? Error::none : Error::io_failure;
}

Error Esp32Bus::i2c_write_read(std::uint8_t address7, etl::span<const std::uint8_t> write_data,
                               etl::span<std::uint8_t> read_data) noexcept {
  if (write_data.empty() || write_data.data() == nullptr) {
    return Error::invalid_argument;
  }
  if (read_data.empty() || read_data.data() == nullptr) {
    return Error::invalid_argument;
  }

  i2c_master_dev_handle_t device = nullptr;
  const Error error = device_for(address7, &device);
  if (error != Error::none) {
    return error;
  }

  // 写寄存器地址 → repeated start → 读 = 寄存器读的标准动作，新驱动的
  // transmit_receive 一次事务就做完了（中间不会插 STOP）。
  const esp_err_t result =
      i2c_master_transmit_receive(device, write_data.data(), write_data.size(), read_data.data(),
                                  read_data.size(), static_cast<int>(bus_i2c_timeout_ms));
  if (result == ESP_ERR_TIMEOUT) {
    return Error::timeout;
  }
  return result == ESP_OK ? Error::none : Error::io_failure;
}

Error Esp32Bus::spi_transfer(etl::span<const std::uint8_t> out,
                             etl::span<std::uint8_t> in) noexcept {
  (void)out;
  (void)in;
  // 屏幕的 SPI 由 esp_lcd 独占（它自己走 DMA）；框架这条总线只管 I2C。
  return Error::unsupported;
}

}  // namespace embark::platform::esp32
