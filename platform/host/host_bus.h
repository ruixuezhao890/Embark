/**
 * 宿主 · 裸总线后端（issues/04）
 *
 * 宿主没有 I2C/SPI 物理总线，所以三条都返回 unsupported。这不是占位实现：
 * 它让上层的"设备未接/能力缺失"分支在宿主机上真的被执行到，而不是只在真机上才第一次跑。
 * 需要模拟设备的测试用 tests/fakes 里的脚本化总线，不在这里堆假的设备模型。
 */
#ifndef EMBARK_PLATFORM_HOST_BUS_H
#define EMBARK_PLATFORM_HOST_BUS_H

#include <cstdint>

#include <embark/hal/bus.h>

namespace embark::platform::host {

class HostBus final : public hal::IBus {
 public:
  Error i2c_write(std::uint8_t address7, etl::span<const std::uint8_t> data) noexcept override;

  Error i2c_write_read(std::uint8_t address7, etl::span<const std::uint8_t> write_data,
                       etl::span<std::uint8_t> read_data) noexcept override;

  Error spi_transfer(etl::span<const std::uint8_t> out,
                     etl::span<std::uint8_t> in) noexcept override;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_BUS_H */
