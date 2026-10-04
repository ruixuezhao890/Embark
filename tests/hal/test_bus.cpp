// HAL 裸总线：宿主/默认一律 unsupported（这台机器没有物理总线），
// 以及把返回值改成 none 之后各条路径的记录与回环行为。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

using embark::Error;
using embark::fakes::FakeBus;

TEST_CASE("IBus：默认三条路径都是 unsupported（不是没实现，是机器没有）") {
  FakeBus bus;
  const std::uint8_t out[4] = {1, 2, 3, 4};
  std::uint8_t in[4] = {};

  CHECK(bus.i2c_write(0x3CU, etl::span<const std::uint8_t>(out, sizeof(out))) ==
        Error::unsupported);
  CHECK(bus.i2c_write_read(0x3CU, etl::span<const std::uint8_t>(out, 1),
                           etl::span<std::uint8_t>(in, sizeof(in))) == Error::unsupported);
  CHECK(bus.spi_transfer(etl::span<const std::uint8_t>(out, sizeof(out)),
                         etl::span<std::uint8_t>(in, sizeof(in))) == Error::unsupported);

  CHECK(bus.captured_length == 0U);  // 失败不动记录
  CHECK(in[0] == 0U);                // 失败不动调用方缓冲
}

TEST_CASE("IBus：i2c 只写成功后记下地址与字节数") {
  FakeBus bus;
  bus.i2c_write_result = Error::none;
  const std::uint8_t out[3] = {0xA5, 0x5A, 0x01};

  CHECK(bus.i2c_write(0x3CU, etl::span<const std::uint8_t>(out, sizeof(out))) == Error::none);

  CHECK(bus.i2c_writes == 1U);
  CHECK(bus.last_address == 0x3CU);
  CHECK(bus.last_write_bytes == 3U);
  CHECK(bus.captured_length == 3U);
  CHECK(bus.captured[0] == 0xA5U);
}

TEST_CASE("IBus：i2c 写读成功后按填充字节写回读缓冲") {
  FakeBus bus;
  bus.i2c_write_read_result = Error::none;
  bus.read_fill_byte = 0x5AU;
  const std::uint8_t reg[2] = {0x00, 0x10};
  std::uint8_t value[4] = {};

  CHECK(bus.i2c_write_read(0x3CU, etl::span<const std::uint8_t>(reg, sizeof(reg)),
                           etl::span<std::uint8_t>(value, sizeof(value))) == Error::none);

  CHECK(bus.i2c_write_reads == 1U);
  CHECK(bus.last_address == 0x3CU);
  CHECK(bus.last_write_bytes == 2U);
  CHECK(bus.last_read_bytes == 4U);
  CHECK(value[0] == 0x5AU);
  CHECK(value[3] == 0x5AU);
}

TEST_CASE("IBus：spi 全双工回环，长度不等时按短的算") {
  FakeBus bus;
  bus.spi_transfer_result = Error::none;
  const std::uint8_t out[4] = {9, 8, 7, 6};
  std::uint8_t in[2] = {};

  CHECK(bus.spi_transfer(etl::span<const std::uint8_t>(out, sizeof(out)),
                         etl::span<std::uint8_t>(in, sizeof(in))) == Error::none);

  CHECK(bus.spi_transfers == 1U);
  CHECK(bus.last_write_bytes == 4U);
  CHECK(bus.last_read_bytes == 2U);
  CHECK(in[0] == 9U);
  CHECK(in[1] == 8U);
}

TEST_CASE("IBus：设备 NACK 之类走 io_failure，调用方缓冲保持不动") {
  FakeBus bus;
  bus.i2c_write_result = Error::io_failure;
  const std::uint8_t out[1] = {0xFF};

  CHECK(bus.i2c_write(0x11U, etl::span<const std::uint8_t>(out, 1)) == Error::io_failure);
  CHECK(bus.last_address == 0x11U);
  CHECK(bus.captured_length == 0U);
}
