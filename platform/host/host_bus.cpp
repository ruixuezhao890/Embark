#include "host_bus.h"

namespace embark::platform::host {

Error HostBus::i2c_write(std::uint8_t /*address7*/,
                         etl::span<const std::uint8_t> /*data*/) noexcept {
  return Error::unsupported;
}

Error HostBus::i2c_write_read(std::uint8_t /*address7*/,
                              etl::span<const std::uint8_t> /*write_data*/,
                              etl::span<std::uint8_t> /*read_data*/) noexcept {
  return Error::unsupported;
}

Error HostBus::spi_transfer(etl::span<const std::uint8_t> /*out*/,
                            etl::span<std::uint8_t> /*in*/) noexcept {
  return Error::unsupported;
}

}  // namespace embark::platform::host
