/**
 * Embark ESP32-S3 · 板载 RTC（PCF85063ATL）实现
 *
 * 寄存器图（NXP PCF85063A 数据手册第 8 章）：
 *   0x00 Control_1  0x01 Control_2  0x02 Offset  0x03 RAM
 *   0x04 Seconds(bit7 = OS)  0x05 Minutes  0x06 Hours  0x07 Days
 *   0x08 Weekdays  0x09 Months  0x0A Years
 * 全部是 BCD 编码；年只有两位数，本驱动按 2000..2099 解释（板子不会活到下一个世纪）。
 * 星期寄存器：0 = 周日 … 6 = 周六（芯片只会自增/回绕，值本身是约定；用 2000-01-01 是周六、
 * 寄存器读出 6 这条实测数据校准过）。
 */
#include "esp32_rtc.h"

#include <cstdio>

#include <middleware/elog/elog.hpp>

#include "esp32_board.h"

namespace embark::platform::esp32 {
namespace {

constexpr std::uint8_t reg_seconds = 0x04;
constexpr std::size_t time_bytes = 7;  // 0x04..0x0A 一次读完（含 weekday，不用）

/// 秒寄存器的 bit7：OS（Oscillator Stop）。置 1 = 振荡器停过，时间不可信。
constexpr std::uint8_t seconds_os_bit = 0x80;
constexpr std::uint8_t seconds_mask = 0x7F;  // 屏蔽 OS 位后才是 BCD 秒

constexpr std::uint8_t bcd_low(std::uint8_t bcd) noexcept { return bcd & 0x0FU; }
constexpr std::uint8_t bcd_high(std::uint8_t bcd) noexcept {
  return static_cast<std::uint8_t>((bcd >> 4U) & 0x0FU);
}
/// 合法 BCD 的高位最大是几（十位上限：秒/分 5、时 2、日 3、月 1、年 9）。
constexpr bool bcd_valid(std::uint8_t bcd, std::uint8_t max_high) noexcept {
  return bcd_low(bcd) <= 9U && bcd_high(bcd) <= max_high;
}
constexpr std::uint8_t bcd_value(std::uint8_t bcd) noexcept {
  return static_cast<std::uint8_t>(bcd_high(bcd) * 10U + bcd_low(bcd));
}

/**
 * 公历年月日 → 1970-01-01 起的天数（Howard Hinnant 的 days_from_civil，无分支表、无循环）。
 * 用整数算而不是 <ctime>：真机没有时区数据库，mktime 会去读 TZ 环境变量。
 */
constexpr std::int64_t days_from_civil(int year, int month, int day) noexcept {
  const int y = year - (month <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int year_of_era = y - era * 400;                                     // [0, 399]
  const int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

/**
 * days_from_civil 的反函数：1970-01-01 起的天数 → 公历年月日（同为 Hinnant 算法）。
 * 写时间要用它把 epoch 拆成寄存器里的六个字段。
 */
constexpr void civil_from_days(std::int64_t days, int& year, int& month, int& day) noexcept {
  const std::int64_t z = days + 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t day_of_era = z - era * 146097;                                  // [0, 146096]
  const std::int64_t year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;  // [0, 399]
  const std::int64_t y = year_of_era + era * 400;
  const std::int64_t day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);         // [0, 365]
  const std::int64_t mp = (5 * day_of_year + 2) / 153;                                // [0, 11]
  const std::int64_t d = day_of_year - (153 * mp + 2) / 5 + 1;                        // [1, 31]
  const std::int64_t m = mp + (mp < 10 ? 3 : -9);                                     // [1, 12]
  year = static_cast<int>(y + (m <= 2 ? 1 : 0));
  month = static_cast<int>(m);
  day = static_cast<int>(d);
}

/// 1970-01-01 是周四 ⇒ (days + 4) % 7，0 = 周日。
constexpr std::uint8_t weekday_from_days(std::int64_t days) noexcept {
  const std::int64_t rem = (days + 4) % 7;
  return static_cast<std::uint8_t>(rem < 0 ? rem + 7 : rem);
}

constexpr std::uint8_t to_bcd(int value) noexcept {
  return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
}

}  // namespace

void format_utc_epoch(std::uint64_t utc_seconds, char* out, std::size_t size) noexcept {
  if (out == nullptr || size == 0U) {
    return;
  }
  const std::int64_t days = static_cast<std::int64_t>(utc_seconds / 86400ULL);
  const std::uint32_t seconds_of_day = static_cast<std::uint32_t>(utc_seconds % 86400ULL);
  int year = 1970;
  int month = 1;
  int day = 1;
  civil_from_days(days, year, month, day);
  const std::uint32_t hour = seconds_of_day / 3600U;
  const std::uint32_t minute = (seconds_of_day / 60U) % 60U;
  const std::uint32_t second = seconds_of_day % 60U;
  (void)std::snprintf(out, size, "%04d-%02d-%02d %02u:%02u:%02uZ", year, month, day,
                      static_cast<unsigned>(hour), static_cast<unsigned>(minute),
                      static_cast<unsigned>(second));
}

Error Pcf85063::read_registers(std::uint8_t reg, std::uint8_t* buffer,
                               std::size_t size) noexcept {
  if (bus_ == nullptr) {
    return Error::not_ready;
  }
  if (buffer == nullptr || size == 0U) {
    return Error::invalid_argument;
  }
  const std::uint8_t address[1] = {reg};
  return bus_->i2c_write_read(rtc_address, etl::span<const std::uint8_t>(address, 1U),
                              etl::span<std::uint8_t>(buffer, size));
}

Error Pcf85063::init(hal::IBus* bus) noexcept {
  if (bus == nullptr) {
    return Error::invalid_argument;
  }
  bus_ = bus;

  std::uint8_t raw[time_bytes] = {};
  const Error read_error = read_registers(reg_seconds, raw, sizeof(raw));
  if (read_error != Error::none) {
    present_ = false;
    // 这条日志是"硬件在不在"的唯一证据，务必说清三种可能：没焊、线不对、上拉缺失。
    ELOG_WARN("板载 RTC：0x{:02X} 读不到（{}）—— PCF85063 没焊 / SDA IO{} SCL IO{} 接反 / 上拉缺失",
              static_cast<unsigned>(rtc_address), static_cast<int>(read_error), bus_pin_sda,
              bus_pin_scl);
    return read_error;
  }

  present_ = true;
  // 原始 7 字节直接进日志：出问题时第一眼就能分清"总线读花了"和"时间本来就荒唐"。
  ELOG_INFO("板载 RTC：0x{:02X} 在线，寄存器 04..0A = {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
            static_cast<unsigned>(rtc_address), static_cast<unsigned>(raw[0]),
            static_cast<unsigned>(raw[1]), static_cast<unsigned>(raw[2]),
            static_cast<unsigned>(raw[3]), static_cast<unsigned>(raw[4]),
            static_cast<unsigned>(raw[5]), static_cast<unsigned>(raw[6]));
  return Error::none;
}

etl::expected<std::uint64_t, Error> Pcf85063::read_epoch_seconds() noexcept {
  if (bus_ == nullptr) {
    return embark::unexpected(Error::not_ready);
  }

  std::uint8_t raw[time_bytes] = {};
  const Error read_error = read_registers(reg_seconds, raw, sizeof(raw));
  if (read_error != Error::none) {
    // 一次读失败不改 present_：总线偶发 NACK 不等于芯片没了。
    return embark::unexpected(read_error);
  }

  if ((raw[0] & seconds_os_bit) != 0U) {
    // OS = 1：振荡器停过（纽扣电池没电 / 首次上电 / 刚焊上去）。寄存器里的数字不是"现在"。
    return embark::unexpected(Error::unsupported);
  }

  const std::uint8_t seconds_raw = static_cast<std::uint8_t>(raw[0] & seconds_mask);
  const bool valid = bcd_valid(seconds_raw, 5U) && bcd_valid(raw[1], 5U) && bcd_valid(raw[2], 2U) &&
                     bcd_valid(raw[3], 3U) && bcd_valid(raw[5], 1U) && bcd_valid(raw[6], 9U) &&
                     bcd_value(seconds_raw) < 60U && bcd_value(raw[1]) < 60U &&
                     bcd_value(raw[2]) < 24U && bcd_value(raw[3]) >= 1U &&
                     bcd_value(raw[3]) <= 31U && bcd_value(raw[5]) >= 1U &&
                     bcd_value(raw[5]) <= 12U;
  if (!valid) {
    ELOG_WARN("RTC：时钟寄存器不是合法 BCD（{:02X} {:02X} {:02X} {:02X} {:02X}）—— 芯片可能在复位态",
              static_cast<unsigned>(seconds_raw), static_cast<unsigned>(raw[1]),
              static_cast<unsigned>(raw[2]), static_cast<unsigned>(raw[3]),
              static_cast<unsigned>(raw[5]));
    return embark::unexpected(Error::corrupt_data);
  }

  const std::uint8_t seconds = bcd_value(seconds_raw);
  const std::uint8_t minutes = bcd_value(raw[1]);
  const std::uint8_t hours = bcd_value(raw[2]);
  const std::uint8_t days = bcd_value(raw[3]);
  const std::uint8_t months = bcd_value(raw[5]);
  const std::uint8_t years = bcd_value(raw[6]);

  const std::int64_t days_since_epoch = days_from_civil(2000 + years, months, days);
  const std::uint64_t epoch_seconds = static_cast<std::uint64_t>(days_since_epoch) * 86400ULL +
                                      static_cast<std::uint64_t>(hours) * 3600ULL +
                                      static_cast<std::uint64_t>(minutes) * 60ULL +
                                      static_cast<std::uint64_t>(seconds);
  return epoch_seconds;
}

Error Pcf85063::write_registers(std::uint8_t reg, const std::uint8_t* data,
                                std::size_t size) noexcept {
  if (bus_ == nullptr) {
    return Error::not_ready;
  }
  if (data == nullptr || size == 0U) {
    return Error::invalid_argument;
  }
  // 一帧 = 寄存器地址 + 数据，I2C 单次写最多 8 字节够用（本驱动只写 7 个时间字节）。
  std::uint8_t frame[8] = {};
  if (size + 1U > sizeof(frame)) {
    return Error::invalid_argument;
  }
  frame[0] = reg;
  for (std::size_t i = 0; i < size; ++i) {
    frame[i + 1U] = data[i];
  }
  return bus_->i2c_write(rtc_address, etl::span<const std::uint8_t>(frame, size + 1U));
}

Error Pcf85063::write_epoch_seconds(std::uint64_t utc_seconds) noexcept {
  if (bus_ == nullptr) {
    return Error::not_ready;
  }

  const std::int64_t days = static_cast<std::int64_t>(utc_seconds / 86400ULL);
  const std::uint32_t second_of_day = static_cast<std::uint32_t>(utc_seconds % 86400ULL);
  int year = 1970;
  int month = 1;
  int day = 1;
  civil_from_days(days, year, month, day);

  const int year_two_digits = year - 2000;
  if (year_two_digits < 0 || year_two_digits > 99) {
    // 芯片只有两位年：2000..2099 之外写进去就是垃圾，宁可拒绝并说清原因。
    ELOG_WARN("RTC 播种：年份 {} 超出 2000..2099，拒绝写入", year);
    return Error::invalid_argument;
  }

  // 秒寄存器 bit7 = OS：写 0 就是"清掉振荡器停过"的标志，这一步是播种的另一半目的。
  // 小时寄存器 bit6 = 12/24 选择：写 0 固定 24 小时制（读取逻辑也按 24 小时算）。
  const std::uint8_t payload[time_bytes] = {
      to_bcd(static_cast<int>(second_of_day % 60U)),
      to_bcd(static_cast<int>((second_of_day / 60U) % 60U)),
      to_bcd(static_cast<int>(second_of_day / 3600U)),
      to_bcd(day),
      weekday_from_days(days),
      to_bcd(month),
      to_bcd(year_two_digits),
  };

  const Error write_error = write_registers(reg_seconds, payload, time_bytes);
  if (write_error != Error::none) {
    ELOG_WARN("RTC 播种：写 0x{:02X} 失败（{}）", static_cast<unsigned>(rtc_address),
              static_cast<int>(write_error));
    return write_error;
  }

  // 回读校验：芯片没吃进去时宁可报失败，也不要让上层以为"已经对时"。
  std::uint8_t check[time_bytes] = {};
  const Error read_error = read_registers(reg_seconds, check, time_bytes);
  if (read_error != Error::none) {
    ELOG_WARN("RTC 播种：回读失败（{}）", static_cast<int>(read_error));
    return read_error;
  }
  // 秒不比：写与读之间真跨过整秒时它会进位，这不是错误；分及以上必须对上。
  const bool same = check[1] == payload[1] && check[2] == payload[2] && check[3] == payload[3] &&
                    check[4] == payload[4] && check[5] == payload[5] && check[6] == payload[6];
  const bool os_cleared = (check[0] & seconds_os_bit) == 0U;
  if (!same || !os_cleared) {
    ELOG_WARN(
        "RTC 播种：回读不符（写到 {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}，读回 "
        "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}，OS 位{}）—— 芯片可能没写进去",
        static_cast<unsigned>(payload[0]), static_cast<unsigned>(payload[1]),
        static_cast<unsigned>(payload[2]), static_cast<unsigned>(payload[3]),
        static_cast<unsigned>(payload[4]), static_cast<unsigned>(payload[5]),
        static_cast<unsigned>(payload[6]), static_cast<unsigned>(check[0]),
        static_cast<unsigned>(check[1]), static_cast<unsigned>(check[2]),
        static_cast<unsigned>(check[3]), static_cast<unsigned>(check[4]),
        static_cast<unsigned>(check[5]), static_cast<unsigned>(check[6]),
        os_cleared ? "已清" : "仍为 1");
    return Error::io_failure;
  }

  ELOG_INFO("RTC 播种：已写入 0x{:02X} 并清 OS 位（分及以上回读一致）",
            static_cast<unsigned>(rtc_address));
  return Error::none;
}

}  // namespace embark::platform::esp32
