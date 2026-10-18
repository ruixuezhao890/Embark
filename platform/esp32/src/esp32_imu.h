/**
 * Embark ESP32-S3 · 板载姿态传感器（QMI8658 六轴）
 *
 * 芯片挂板载 I2C（I2C_NUM_1，SDA IO11 / SCL IO10，400 kHz）—— 与 RTC PCF85063 同一条总线，
 * 所以这里不建总线，只借框架的 hal::IBus 收发字节（板级常量见 esp32_board.h 的 bus 一节）。
 * INT1 = IO13 / INT2 = IO12 在本驱动里**不用**：数据靠轮询读（读 12 字节 < 1 ms），
 * 中断留给以后真要做「摇一摇」低功耗唤醒时再开。
 *
 * ## 地址是探出来的，不是写死的
 *
 * QMI8658 的 SA0 脚决定 7 位地址是 0x6A（SA0=0）还是 0x6B（SA0=1）。仓库里没有这块板的确切
 * 取值（RTC 的 0x51 是图纸确认过的，IMU 没有），所以 init() 依次探两个地址、用 WHO_AM_I
 * 回读值做判据 —— 探到的那个写进 address_ 并打进日志。谁哪天确认了，把 kAddressCandidates
 * 收成一项即可（不必改逻辑）。
 *
 * ## 量程与换算
 *
 * 加速度 ±4 g ⇒ 8192 LSB/g；角速度 ±512 dps ⇒ 64 LSB/dps；两者都是 16 位有符号、小端。
 * 换算成 g / dps 在这里做，hal::IImu 只给物理量（见 include/embark/hal/imu.h 的理由）。
 * 数据率 250 Hz：比 LVGL 一帧（约 30 ms）快一个数量级，App 每帧读一次拿到的一定是新样本。
 *
 * 原始值 → 物理量：raw / 8192.0 g、raw / 64.0 dps。这两个数只要量程不变就不用再算。
 */
#ifndef EMBARK_PLATFORM_ESP32_IMU_H
#define EMBARK_PLATFORM_ESP32_IMU_H

#include <cstdint>

#include <embark/error.h>
#include <embark/hal/bus.h>
#include <embark/hal/imu.h>

namespace embark::platform::esp32 {

class Esp32Imu final : public hal::IImu {
 public:
  Esp32Imu() noexcept = default;
  Esp32Imu(const Esp32Imu&) = delete;
  Esp32Imu& operator=(const Esp32Imu&) = delete;
  ~Esp32Imu() override = default;

  /**
   * 探地址（WHO_AM_I）→ 软复位 → 配 CTRL1 自增/小端 → 配量程与数据率 → 开两轴。
   * 幂等：已就绪时直接返回 none。
   *
   * 返回 not_found = 两个候选地址都没应答；io_failure = 有应答但配置写不进去。
   * 调用方（Esp32Hal::init）**不**把它当致命错误：没有 IMU 的板子照样要能跑。
   */
  [[nodiscard]] Error init(hal::IBus* bus) noexcept;

  /// IImu 契约里的无参版本：转调 init(bus_)。总线由 Esp32Hal 在启动序列里先 attach_bus
  /// 挂上（Esp32Imu 是成员对象，构造期总线还没 init，不能在构造函数里拿）。
  [[nodiscard]] Error init() noexcept override;

  /// 只挂总线、不碰芯片 —— 让 Esp32Hal::init 能把「先 init 总线、再 init 各设备」
  /// 这个顺序写在一处，而不是把总线指针塞进 IMU 的构造函数。
  void attach_bus(hal::IBus* bus) noexcept { bus_ = bus; }

  [[nodiscard]] Error read(hal::ImuSample& out) noexcept override;

  /// 芯片应答过（init 探到了）。不代表读数可信（那要看 read() 的返回值）。
  [[nodiscard]] bool present() const noexcept { return present_; }

  /// 探到的 7 位地址；没探到时是 0。日志与验收脚本拿它判断「这颗芯片到底挂在哪」。
  [[nodiscard]] std::uint8_t address() const noexcept { return address_; }

 private:
  [[nodiscard]] Error read_registers(std::uint8_t reg, std::uint8_t* buffer,
                                     std::size_t size) noexcept;
  [[nodiscard]] Error write_register(std::uint8_t reg, std::uint8_t value) noexcept;
  /// 在候选地址里找出应答 WHO_AM_I 的那一个，写进 address_。
  [[nodiscard]] Error probe_address() noexcept;
  /// 配 CTRL1/2/3/7。复位之后要重跑一次（软复位会把它们清回默认）。
  [[nodiscard]] Error configure() noexcept;

  hal::IBus* bus_ = nullptr;
  std::uint8_t address_ = 0;
  bool present_ = false;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_IMU_H */
