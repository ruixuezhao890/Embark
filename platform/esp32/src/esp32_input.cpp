/**
 * Embark ESP32-S3 · 触摸输入后端实现（issues/11）
 */
#include "esp32_input.h"

#include <embark/error.h>
#include <embark_limits.h>
#include <middleware/elog/elog.hpp>

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32_board.h"

namespace embark::platform::esp32 {
namespace {

/// 单次 I2C 事务超时（毫秒）：400 kHz 下 29 字节远不到 1 ms，50 ms 已经是"设备掉了"。
constexpr int i2c_timeout_ms = 50;

/// 27 字节点报文里第 0 点的位置（单点 UI 用不上其余 4 个点）。
constexpr std::size_t point0_x_high_byte = 1U;
constexpr std::size_t point0_y_high_byte = 2U;
constexpr std::size_t point0_low_nibbles_byte = 3U;

/// 中断计数寄存器（0xD005）与两个模式寄存器的地址高低字节（I2C 先发 2 字节地址）。
constexpr std::uint8_t count_reg_high = static_cast<std::uint8_t>(touch_reg_count >> 8);
constexpr std::uint8_t count_reg_low = static_cast<std::uint8_t>(touch_reg_count & 0x00FFU);
constexpr std::uint8_t debug_reg_high = static_cast<std::uint8_t>(touch_reg_debug_mode >> 8);
constexpr std::uint8_t debug_reg_low = static_cast<std::uint8_t>(touch_reg_debug_mode & 0x00FFU);
constexpr std::uint8_t normal_reg_high = static_cast<std::uint8_t>(touch_reg_normal_mode >> 8);
constexpr std::uint8_t normal_reg_low = static_cast<std::uint8_t>(touch_reg_normal_mode & 0x00FFU);

std::uint32_t now_ms() noexcept {
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

/// 夹到 [0, limit-1]：边缘手指偶尔会报出面板外的坐标，不能让后面的镜像运算下溢。
std::uint16_t clamp_axis(std::uint16_t value, std::size_t limit) noexcept {
  if (value < limit) {
    return value;
  }
  return static_cast<std::uint16_t>(limit - 1U);
}

}  // namespace

Esp32Input::~Esp32Input() {
  if (device_ != nullptr) {
    (void)i2c_master_bus_rm_device(device_);
    device_ = nullptr;
  }
  if (bus_owned_ && bus_ != nullptr) {
    (void)i2c_del_master_bus(bus_);  // 别人建的总线不归我们拆
    bus_owned_ = false;
  }
  bus_ = nullptr;
}

Error Esp32Input::init() noexcept {
  if (device_ != nullptr) {
    return Error::none;  // 幂等
  }

  // 1) 触摸自己的 I2C 控制器（I2C_NUM_0，IO1/IO3）。
  //    逐字段赋值而不是指定初始化器：C++17 下后者是编译器扩展（-Wpedantic 会叫）。
  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = static_cast<i2c_port_num_t>(touch_i2c_port);
  // 引脚常量在 esp32_board.h 里是 int（那个头不引 IDF 头）；IDF 要 gpio_num_t，显式转。
  bus_config.sda_io_num = static_cast<gpio_num_t>(touch_pin_sda);
  bus_config.scl_io_num = static_cast<gpio_num_t>(touch_pin_scl);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  esp_err_t error = i2c_new_master_bus(&bus_config, &bus_);
  if (error == ESP_ERR_INVALID_STATE) {
    const esp_err_t found =
        i2c_master_get_bus_handle(static_cast<i2c_port_num_t>(touch_i2c_port), &bus_);
    if (found != ESP_OK) {
      ELOG_ERROR("触摸：I2C{} 已初始化但取不到句柄：{}", touch_i2c_port, esp_err_to_name(found));
      bus_ = nullptr;
      return Error::io_failure;
    }
    ELOG_WARN("触摸：I2C{} 上已有总线，沿用现有句柄", touch_i2c_port);
  } else if (error != ESP_OK) {
    ELOG_ERROR("触摸：i2c_new_master_bus(I2C{}, SDA {} / SCL {}) 失败：{}", touch_i2c_port,
               touch_pin_sda, touch_pin_scl, esp_err_to_name(error));
    bus_ = nullptr;
    return Error::io_failure;
  } else {
    bus_owned_ = true;
  }

  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = touch_address;
  device_config.scl_speed_hz = touch_i2c_hz;

  error = i2c_master_bus_add_device(bus_, &device_config, &device_);
  if (error != ESP_OK) {
    ELOG_ERROR("触摸：挂载从设备 0x{:02X} 失败：{}", static_cast<unsigned>(touch_address),
               esp_err_to_name(error));
    device_ = nullptr;
    return Error::io_failure;
  }

  // 2) 硬复位：厂商驱动的顺序是先拉低（RESET_ACTIVE）、再拉高。
  gpio_config_t reset_config = {};
  reset_config.pin_bit_mask = 1ULL << static_cast<unsigned>(touch_pin_rst);
  reset_config.mode = GPIO_MODE_OUTPUT;
  reset_config.pull_up_en = GPIO_PULLUP_DISABLE;
  reset_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  reset_config.intr_type = GPIO_INTR_DISABLE;

  error = gpio_config(&reset_config);
  if (error == ESP_OK) {
    (void)gpio_set_level(static_cast<gpio_num_t>(touch_pin_rst), 0);
    vTaskDelay(pdMS_TO_TICKS(touch_reset_low_ms));
    (void)gpio_set_level(static_cast<gpio_num_t>(touch_pin_rst), 1);
    vTaskDelay(pdMS_TO_TICKS(touch_reset_high_ms));
  } else {
    ELOG_WARN("触摸：复位脚 GPIO{} 配置失败（{}），继续按已复位状态走", touch_pin_rst,
              esp_err_to_name(error));
  }

  // 3) 顺手读一次 CST328 自报的分辨率：实机方向不对时，这行日志是第一手证据。
  //    厂商驱动的顺序：先切调试模式（往 0xD101 写 0 字节），读完切回正常模式（0xD109）。
  std::uint8_t resolution[4] = {};
  const std::uint8_t debug_mode[2] = {debug_reg_high, debug_reg_low};
  bool resolution_ok = false;
  if (i2c_master_transmit(device_, debug_mode, sizeof(debug_mode), i2c_timeout_ms) == ESP_OK) {
    resolution_ok = read_registers(touch_reg_res_x, resolution, sizeof(resolution)) == Error::none;
  }
  const std::uint8_t normal_mode[2] = {normal_reg_high, normal_reg_low};
  (void)i2c_master_transmit(device_, normal_mode, sizeof(normal_mode), i2c_timeout_ms);

  if (resolution_ok) {
    const std::uint16_t res_x = static_cast<std::uint16_t>(
        resolution[0] | (static_cast<std::uint16_t>(resolution[1]) << 8));
    const std::uint16_t res_y = static_cast<std::uint16_t>(
        resolution[2] | (static_cast<std::uint16_t>(resolution[3]) << 8));
    ELOG_INFO(
        "CST328 就绪：地址 0x{:02X}，自报分辨率 X={} / Y={}（屏幕 {}×{}；方向不对先看这几个数）",
        static_cast<unsigned>(touch_address), static_cast<unsigned>(res_x),
        static_cast<unsigned>(res_y), static_cast<unsigned>(embark::display_width),
        static_cast<unsigned>(embark::display_height));
  } else {
    ELOG_WARN("CST328：分辨率读不到（不影响出点，只是少一条方向证据）");
  }
  return Error::none;
}

Error Esp32Input::read_registers(std::uint16_t reg, std::uint8_t* buffer,
                                 std::size_t size) noexcept {
  if (device_ == nullptr) {
    return Error::not_ready;
  }
  if (buffer == nullptr || size == 0U) {
    return Error::invalid_argument;
  }

  const std::uint8_t address[2] = {static_cast<std::uint8_t>(reg >> 8),
                                   static_cast<std::uint8_t>(reg & 0x00FFU)};
  const esp_err_t error =
      i2c_master_transmit_receive(device_, address, sizeof(address), buffer, size, i2c_timeout_ms);
  if (error == ESP_OK) {
    return Error::none;
  }
  return (error == ESP_ERR_TIMEOUT) ? Error::timeout : Error::io_failure;
}

void Esp32Input::clear_interrupt() noexcept {
  if (device_ == nullptr) {
    return;
  }
  // 厂商驱动的口径：往 0xD005 写 1 字节 0。写不进去也不致命 —— 下一拍照样能问点数，
  // 只是这拍的中断标志会留着（最坏情况是连续几拍都读到同一个点，press 去重会挡住）。
  const std::uint8_t clear[3] = {count_reg_high, count_reg_low, 0x00};
  (void)i2c_master_transmit(device_, clear, sizeof(clear), i2c_timeout_ms);
}

Error Esp32Input::read_point(std::uint16_t& x, std::uint16_t& y, bool& touched) noexcept {
  touched = false;

  std::uint8_t count = 0;
  const Error count_error = read_registers(touch_reg_count, &count, 1U);
  if (count_error != Error::none) {
    ++read_failures_;
    return count_error;
  }

  const std::uint8_t points = static_cast<std::uint8_t>(count & 0x0FU);
  if (points == 0U || points > touch_max_points) {
    // 没触摸；或者报了个不合理的点数（I2C 抖一下也会这样）—— 清中断，当这一拍没有手指。
    clear_interrupt();
    return Error::none;
  }

  std::uint8_t packet[touch_point_bytes] = {};
  const Error read_error = read_registers(touch_reg_points, packet, sizeof(packet));
  // 读没读成都要清中断：厂商驱动的顺序就是"读点 → 写 0"，不清下一拍就不来了。
  clear_interrupt();
  if (read_error != Error::none) {
    ++read_failures_;
    return read_error;
  }

  // 第 0 点：x = packet[1] << 4 | packet[3] >> 4；y = packet[2] << 4 | packet[3] & 0x0F。
  std::uint16_t raw_x = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(packet[point0_x_high_byte]) << 4) |
      (static_cast<std::uint16_t>(packet[point0_low_nibbles_byte]) >> 4));
  std::uint16_t raw_y = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(packet[point0_y_high_byte]) << 4) |
      (static_cast<std::uint16_t>(packet[point0_low_nibbles_byte]) & 0x0FU));

  if (touch_swap_xy) {
    const std::uint16_t temporary = raw_x;
    raw_x = raw_y;
    raw_y = temporary;
  }
  raw_x = clamp_axis(raw_x, embark::display_width);
  raw_y = clamp_axis(raw_y, embark::display_height);
  if (touch_mirror_x) {
    raw_x = static_cast<std::uint16_t>(embark::display_width - 1U - raw_x);
  }
  if (touch_mirror_y) {
    raw_y = static_cast<std::uint16_t>(embark::display_height - 1U - raw_y);
  }

  x = raw_x;
  y = raw_y;
  touched = true;
  return Error::none;
}

etl::expected<bool, Error> Esp32Input::poll(hal::InputEvent& event) noexcept {
  if (device_ == nullptr) {
    return embark::unexpected(Error::not_ready);
  }

  // 触摸是"电平"语义，而 poll 每帧都会被叫（UI 循环 5 ms）—— 按 10 ms 节流，
  // 既给 I2C 让路，也避免把同一次按住刷成几百个事件。
  const std::uint32_t stamp = now_ms();
  if (last_poll_ms_ != 0U && (stamp - last_poll_ms_) < touch_poll_period_ms) {
    return false;
  }
  last_poll_ms_ = stamp;

  std::uint16_t x = 0;
  std::uint16_t y = 0;
  bool touched = false;
  const Error error = read_point(x, y, touched);
  if (error != Error::none) {
    // I2C 抖一下不该变成 App 里的一个错误事件：这里只记一笔（超时连日志都不刷，
    // 免得总线真断了把日志刷爆），poll 如实回答"这一拍没有事件"。
    if (error != Error::timeout) {
      ELOG_WARN("触摸读取失败：{}", to_string(error));
    }
    return false;
  }

  event.timestamp_ms = stamp;
  event.key = 0;  // 指针事件：按约定 key 恒 0

  if (touched && !pressed_) {
    pressed_ = true;
    pressed_x_ = x;
    pressed_y_ = y;
    ++presses_;
    event.kind = hal::InputEventKind::press;
    event.x = x;
    event.y = y;
    return true;
  }
  if (touched && pressed_) {
    if (x == pressed_x_ && y == pressed_y_) {
      return false;  // 位置没变就没有新事件（框架与 LVGL 都不需要重复点）
    }
    pressed_x_ = x;
    pressed_y_ = y;
    event.kind = hal::InputEventKind::move;
    event.x = x;
    event.y = y;
    return true;
  }
  if (!touched && pressed_) {
    pressed_ = false;
    ++releases_;
    event.kind = hal::InputEventKind::release;
    event.x = pressed_x_;
    event.y = pressed_y_;
    return true;
  }
  return false;  // 一直没触摸
}

}  // namespace embark::platform::esp32
