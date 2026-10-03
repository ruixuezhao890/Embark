// 测试替身：假总线。
//
// 默认与宿主后端同款：这台机器没有物理总线，三条路径一律 unsupported。测试可以逐个把
// 返回值改成 none/io_failure，并检查调用参数，从而把上层写好的错误分支都跑一遍。
#pragma once

#include <embark/hal/bus.h>

#include <cstddef>
#include <cstdint>

namespace embark::fakes {

class FakeBus final : public hal::IBus {
 public:
  FakeBus() noexcept = default;

  Error i2c_write(std::uint8_t address7,
                  etl::span<const std::uint8_t> data) noexcept override {
    ++i2c_writes;
    last_address = address7;
    last_write_bytes = data.size();
    if (i2c_write_result == Error::none) {
      capture(data);
    }
    return i2c_write_result;
  }

  Error i2c_write_read(std::uint8_t address7,
                       etl::span<const std::uint8_t> write_data,
                       etl::span<std::uint8_t> read_data) noexcept override {
    ++i2c_write_reads;
    last_address = address7;
    last_write_bytes = write_data.size();
    last_read_bytes = read_data.size();
    if (i2c_write_read_result == Error::none) {
      capture(write_data);
      fill(read_data);
    }
    return i2c_write_read_result;
  }

  Error spi_transfer(etl::span<const std::uint8_t> out, etl::span<std::uint8_t> in) noexcept override {
    ++spi_transfers;
    last_write_bytes = out.size();
    last_read_bytes = in.size();
    if (spi_transfer_result == Error::none) {
      capture(out);
      // 全双工回环：把发出去的原样收回来（SPI 的经典自测）。
      const std::size_t n = out.size() < in.size() ? out.size() : in.size();
      for (std::size_t i = 0; i < n; ++i) {
        in[i] = out[i];
      }
    }
    return spi_transfer_result;
  }

  // 旋钮与观测点。
  Error i2c_write_result = Error::unsupported;
  Error i2c_write_read_result = Error::unsupported;
  Error spi_transfer_result = Error::unsupported;
  std::uint8_t read_fill_byte = 0xA5;
  std::uint8_t last_address = 0;
  std::size_t last_write_bytes = 0;
  std::size_t last_read_bytes = 0;
  std::size_t i2c_writes = 0;
  std::size_t i2c_write_reads = 0;
  std::size_t spi_transfers = 0;
  etl::array<std::uint8_t, 32> captured{};
  std::size_t captured_length = 0;

 private:
  void capture(etl::span<const std::uint8_t> data) noexcept {
    const std::size_t n = data.size() < captured.size() ? data.size() : captured.size();
    for (std::size_t i = 0; i < n; ++i) {
      captured[i] = data[i];
    }
    captured_length = n;
  }

  void fill(etl::span<std::uint8_t> data) noexcept {
    for (std::size_t i = 0; i < data.size(); ++i) {
      data[i] = read_fill_byte;
    }
  }
};

}  // namespace embark::fakes
