/**
 * Embark ESP32-S3 · 板载姿态传感器实现（QMI8658）
 *
 * 寄存器图（QMI8658A/C 数据手册 Rev D「Register Map」章，2026-10-18 按公开驱动交叉核对）：
 *   0x00 WHO_AM_I（应为 0x05）  0x01 REVISION_ID
 *   0x02 CTRL1  bit6 ADDR_AI（读自动递增）/ bit5 BE（1=大端）/ bit0 SENSOR_DISABLE
 *   0x03 CTRL2  bit7 A_ST  bit6:4 A_FS  bit3:0 A_ODR
 *   0x04 CTRL3  bit7 G_ST  bit6:4 G_FS  bit3:0 G_ODR
 *   0x06 CTRL5  低通（本驱动不动它，理由见 configure() 的注释）
 *   0x08 CTRL7  bit0 aEN / bit1 gEN（bit7 SYNC_SMPL）
 *   0x60 RESET  写 0xB0 软复位
 *   0x33..0x40  温度 L/H + 加速度 X/Y/Z L/H + 角速度 X/Y/Z L/H（14 字节，一次连读）
 * 全部 16 位量都是**小端**（CTRL1 的 BE 位保持 0）。
 */
#include "esp32_imu.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <middleware/elog/elog.hpp>

#include "esp32_board.h"

namespace embark::platform::esp32 {
namespace {

// --- 地址（SA0 脚决定，探出来）-------------------------------------------------
constexpr std::uint8_t kAddressCandidates[] = {0x6A, 0x6B};
constexpr std::uint8_t kWhoAmI = 0x00;
constexpr std::uint8_t kWhoAmIExpected = 0x05;

// --- 控制寄存器 ----------------------------------------------------------------
constexpr std::uint8_t kCtrl1 = 0x02;
constexpr std::uint8_t kCtrl2 = 0x03;
constexpr std::uint8_t kCtrl3 = 0x04;
constexpr std::uint8_t kCtrl7 = 0x08;
constexpr std::uint8_t kReset = 0x60;
constexpr std::uint8_t kSoftReset = 0xB0;

// CTRL1：ADDR_AI（读自动递增；不加这一位，14 字节连读会一直读到同一个寄存器）+
//        小端（BE=0）+ 4 线 SPI（不影响 I2C）。
constexpr std::uint8_t kCtrl1Value = 0x40;
// CTRL2：量程 ±4 g（A_FS=0x1 << 4）+ 数据率 250 Hz（A_ODR=0x05）。
constexpr std::uint8_t kCtrl2Value = 0x15;
// CTRL3：量程 ±512 dps（G_FS=0x5 << 4）+ 数据率 250 Hz（G_ODR=0x05）。
constexpr std::uint8_t kCtrl3Value = 0x55;
// CTRL7：开加速度计（bit0）+ 开陀螺（bit1）。
constexpr std::uint8_t kCtrl7Value = 0x03;

// --- 数据寄存器 ----------------------------------------------------------------
constexpr std::uint8_t kDataStart = 0x33;  // TEMP_OUT_L
constexpr std::size_t kDataBytes = 14;     // 温度 2 + 加速度 6 + 角速度 6

// --- 换算系数（与上面的量程一一对应，改量程必须同步改这里）---------------------
constexpr float kAccelLsbPerG = 8192.0F;  // ±4 g
constexpr float kGyroLsbPerDps = 64.0F;   // ±512 dps
constexpr float kTempLsbPerC = 256.0F;

// 软复位后要给芯片落地时间；数据手册写 20 ms 量级，这里取 30 ms 留余量。
constexpr std::uint32_t kResetSettleMs = 30;

constexpr std::int16_t make_int16(std::uint8_t low, std::uint8_t high) noexcept {
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(low) |
                                   (static_cast<std::uint16_t>(high) << 8U));
}

}  // namespace

Error Esp32Imu::read_registers(std::uint8_t reg, std::uint8_t* buffer, std::size_t size) noexcept {
  if (bus_ == nullptr) {
    return Error::not_ready;
  }
  if (buffer == nullptr || size == 0U) {
    return Error::invalid_argument;
  }
  const std::uint8_t address[1] = {reg};
  return bus_->i2c_write_read(address_, etl::span<const std::uint8_t>(address, 1U),
                              etl::span<std::uint8_t>(buffer, size));
}

Error Esp32Imu::write_register(std::uint8_t reg, std::uint8_t value) noexcept {
  if (bus_ == nullptr) {
    return Error::not_ready;
  }
  const std::uint8_t frame[2] = {reg, value};
  return bus_->i2c_write(address_, etl::span<const std::uint8_t>(frame, 2U));
}

Error Esp32Imu::probe_address() noexcept {
  for (const std::uint8_t candidate : kAddressCandidates) {
    address_ = candidate;
    std::uint8_t who = 0;
    if (read_registers(kWhoAmI, &who, 1U) != Error::none) {
      continue;  // 这个地址没人应答：换下一个
    }
    if (who == kWhoAmIExpected) {
      ELOG_INFO("板载 IMU：0x{:02X} 在线，WHO_AM_I = 0x{:02X}", static_cast<unsigned>(candidate),
                static_cast<unsigned>(who));
      return Error::none;
    }
    ELOG_WARN("板载 IMU：0x{:02X} 有应答但 WHO_AM_I = 0x{:02X}（期望 0x05）—— 不是这颗芯片",
              static_cast<unsigned>(candidate), static_cast<unsigned>(who));
  }
  address_ = 0;
  return Error::not_found;
}

Error Esp32Imu::configure() noexcept {
  const struct {
    std::uint8_t reg;
    std::uint8_t value;
    const char* name;
  } settings[] = {
      {kCtrl1, kCtrl1Value, "CTRL1(自增/小端)"},
      {kCtrl2, kCtrl2Value, "CTRL2(±4 g / 250 Hz)"},
      {kCtrl3, kCtrl3Value, "CTRL3(±512 dps / 250 Hz)"},
      {kCtrl7, kCtrl7Value, "CTRL7(开两轴)"},
  };
  // 有意不配 CTRL5（低通）：两组公开驱动的 CTRL5 位图互相矛盾（一说是高半字节陀螺、低半字节
  // 加速度，一说是 bit6:5 加一个使能位），而手上没有硬件可验。低通默认关闭只让读数更噪，
  // 不会让读数错 —— 猜一个位图反而可能把量程/数据率写坏。要加按数据手册 Rev D 的 CTRL5 章补。
  for (const auto& setting : settings) {
    const Error error = write_register(setting.reg, setting.value);
    if (error != Error::none) {
      ELOG_WARN("板载 IMU：写 {} 失败（{}）", setting.name, static_cast<int>(error));
      return error;
    }
  }
  return Error::none;
}

Error Esp32Imu::init(hal::IBus* bus) noexcept {
  if (bus == nullptr) {
    return Error::invalid_argument;
  }
  if (present_) {
    return Error::none;  // 幂等
  }
  bus_ = bus;

  const Error probe_error = probe_address();
  if (probe_error != Error::none) {
    present_ = false;
    // 这条日志是「硬件到底在不在」的唯一证据，说清三种可能：没焊、地址不对、上拉缺失。
    ELOG_WARN(
        "板载 IMU：0x6A/0x6B 都没应答 —— 芯片没焊 / SA0 接法不同 / SDA IO{} SCL IO{} 上拉缺失",
        static_cast<int>(bus_pin_sda), static_cast<int>(bus_pin_scl));
    return probe_error;
  }

  // 软复位：芯片可能停在上一轮固件留下的配置里（固件重刷不清电），先回到已知状态。
  // 复位会把 CTRL1 的清零（含 ADDR_AI），所以 configure() 必须在复位之后重跑一遍。
  const Error reset_error = write_register(kReset, kSoftReset);
  if (reset_error != Error::none) {
    ELOG_WARN("板载 IMU：软复位写不进去（{}）", static_cast<int>(reset_error));
    present_ = false;
    return reset_error;
  }
  vTaskDelay(pdMS_TO_TICKS(kResetSettleMs));

  const Error config_error = configure();
  if (config_error != Error::none) {
    present_ = false;
    return config_error;
  }

  present_ = true;
  ELOG_INFO("板载 IMU：已就绪（±4 g / ±512 dps / 250 Hz，地址 0x{:02X}）",
            static_cast<unsigned>(address_));
  return Error::none;
}

Error Esp32Imu::init() noexcept {
  if (present_) {
    return Error::none;  // 幂等（这里也要挡一道：init(bus) 里那道挡不住重复调用 attach 后的路径）
  }
  if (bus_ == nullptr) {
    return Error::not_ready;  // 装配点忘了 attach_bus
  }
  return init(bus_);
}

Error Esp32Imu::read(hal::ImuSample& out) noexcept {
  if (!present_) {
    return Error::not_ready;
  }
  std::uint8_t raw[kDataBytes] = {};
  const Error error = read_registers(kDataStart, raw, sizeof(raw));
  if (error != Error::none) {
    // 一次读失败不改 present_：总线偶发 NACK 不等于芯片没了。但也不给旧值 —— 上位机
    // 看到的是 io_failure，而不是「这一帧恰好静止」。
    return error;
  }

  const std::int16_t temp_raw = make_int16(raw[0], raw[1]);
  const std::int16_t ax = make_int16(raw[2], raw[3]);
  const std::int16_t ay = make_int16(raw[4], raw[5]);
  const std::int16_t az = make_int16(raw[6], raw[7]);
  const std::int16_t gx = make_int16(raw[8], raw[9]);
  const std::int16_t gy = make_int16(raw[10], raw[11]);
  const std::int16_t gz = make_int16(raw[12], raw[13]);

  out.accel_x = static_cast<float>(ax) / kAccelLsbPerG;
  out.accel_y = static_cast<float>(ay) / kAccelLsbPerG;
  out.accel_z = static_cast<float>(az) / kAccelLsbPerG;
  out.gyro_x = static_cast<float>(gx) / kGyroLsbPerDps;
  out.gyro_y = static_cast<float>(gy) / kGyroLsbPerDps;
  out.gyro_z = static_cast<float>(gz) / kGyroLsbPerDps;
  out.temperature_c = static_cast<float>(temp_raw) / kTempLsbPerC;
  return Error::none;
}

}  // namespace embark::platform::esp32
