/**
 * Embark ESP32-S3 · 板载 RTC（PCF85063ATL）
 *
 * 芯片挂板载 I2C（I2C_NUM_1，SDA IO11 / SCL IO10），7 位地址 0x51 —— 与 IMU QMI8658 同一条
 * 总线，所以这里不建总线，只借用框架的 hal::IBus 收发字节（板级常量见 esp32_board.h）。
 *
 * 做两件事：读当前时间（框架的 ITime 只要这一件），以及"被播种"时写一次时间并清 OS 位。
 * 写时间这条路是 2026-10-17 加的，边界很窄 —— 只有 Esp32Time 在发现芯片报 OS=1（振荡器停过，
 * 时间不可信）时，才拿**固件编译时刻**写一次（见 esp32_time.cpp 的 seed_from_build_time）。
 * 我们仍然没有可信时间源（没网、没上位机通道），所以这个写入只承诺"把芯片从出厂值拉回现在
 * ±几分钟"，不承诺权威。故意不做的：
 *   * 秒中断 / 闹钟 —— Esp32Time 用 tick 推算秒，不需要芯片再唤醒我们（IO9 的 RTC_INT 空着）；
 *   * 校准寄存器（0x02 Offset）—— 这块板没有温度参考，动了反而引入人为偏差；
 *   * 小时制切换 —— 固定 24 小时制（写小时寄存器时 bit6=0），12 小时制会让读取逻辑多一个分支。
 *
 * 时间口径：**UTC**。换算成本地时间是 App 层的事（app/common/local_clock.h 的 +8 小时），
 * 驱动与 HAL 都不碰时区。
 */
#ifndef EMBARK_PLATFORM_ESP32_RTC_H
#define EMBARK_PLATFORM_ESP32_RTC_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <embark/hal/bus.h>
#include <middleware/etl/expected.h>

namespace embark::platform::esp32 {

/**
 * 把 UTC epoch 秒格式化成 "YYYY-MM-DD HH:MM:SSZ" 写进 out（够 21 字节含结尾 0）。
 *
 * 放在这里是因为公历换算的代码本来就在本文件；日志里给人类看的日期比裸 epoch 有用得多
 * （"播种成 2026-10-17 04:05:06Z" 一眼能判断对不对，1792195200 得拿计算器）。
 */
void format_utc_epoch(std::uint64_t utc_seconds, char* out, std::size_t size) noexcept;

class Pcf85063 {
 public:
  Pcf85063() noexcept = default;
  Pcf85063(const Pcf85063&) = delete;
  Pcf85063& operator=(const Pcf85063&) = delete;
  ~Pcf85063() = default;

  /**
   * 挂上总线并探一次芯片（读 0x04..0x0A 的时钟寄存器）。幂等。
   *
   * 返回 none = 芯片应答了；其余 = 读不动（没焊 / 引脚不对 / 上拉缺失）。调用方**不该**
   * 把这里的失败当致命错误：没有 RTC 的板子照样要能跑，只是 epoch_ms() 会如实报 unsupported。
   */
  [[nodiscard]] Error init(hal::IBus* bus) noexcept;

  /// 芯片应答过（init 探到了）。注意它不代表"时间可信"——那要看 read_epoch_seconds()。
  [[nodiscard]] bool present() const noexcept { return present_; }

  /**
   * 读现在的 UTC epoch 秒。
   *
   * Error::unsupported = 振荡器停过（秒寄存器 bit7 的 OS 位：电池没电 / 首次上电 / 刚焊上），
   * 这时寄存器里的数字不是"现在"，宁可报不可用也不编一个时间；
   * Error::corrupt_data = 寄存器不是合法 BCD（芯片在复位态或总线读花了）。
   */
  [[nodiscard]] etl::expected<std::uint64_t, Error> read_epoch_seconds() noexcept;

  /**
   * 把芯片时间设成 utc_seconds（UTC epoch 秒），写完**回读校验**。
   *
   * 一次写 0x04..0x0A 七个 BCD 字节，秒寄存器的 OS 位写 0 —— 清 OS 位是这一步的一半目的：
   * 不清的话下次开机依然报"时间不可信"。写失败返回 io_failure（总线 NACK），回读对不上
   * 也返回 io_failure（芯片没吃进去，比假装成功好）。
   *
   * 注意它**不**校验入参是否荒唐：调用方给 2000 年它也照写。可信性由调用方负责。
   */
  [[nodiscard]] Error write_epoch_seconds(std::uint64_t utc_seconds) noexcept;

 private:
  /// 写 1 字节寄存器地址 → repeated start → 读 size 字节。
  [[nodiscard]] Error read_registers(std::uint8_t reg, std::uint8_t* buffer,
                                     std::size_t size) noexcept;

  /// 一次连续写：buffer[0] 是寄存器地址，buffer[1..] 是数据。
  [[nodiscard]] Error write_registers(std::uint8_t reg, const std::uint8_t* data,
                                      std::size_t size) noexcept;

  hal::IBus* bus_ = nullptr;
  bool present_ = false;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_RTC_H */
