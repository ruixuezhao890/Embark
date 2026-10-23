/**
 * HAL · 裸总线（spec §8 的"最薄一层"）
 *
 * 这一组接口故意只到"收发字节"为止：谁接在哪、上拉多少、片选怎么走，全是平台/驱动的事，
 * 不进框架。真正的设备驱动（屏幕、触摸、传感器）落在 app 或平台侧，通过 Context::bus 拿到
 * 总线句柄，而不是让框架认识每种芯片。
 *
 * 宿主后端一律返回 Error::unsupported —— 它不是"没实现"，而是"这台机器本来就没有物理总线"，
 * 这正好让上层写好的错误分支在宿主机上被真跑一遍。
 */
#ifndef EMBARK_HAL_BUS_H
#define EMBARK_HAL_BUS_H

#include <cstdint>

#include <embark/error.h>
#include <middleware/etl/span.h>

namespace embark::hal {

class IBus {
 public:
  virtual ~IBus() = default;

  /// 只写：start → 7 位地址 + 写标志 → 数据 → stop。设备没响应（NACK）→ io_failure。
  virtual Error i2c_write(std::uint8_t address7, etl::span<const std::uint8_t> data) noexcept = 0;

  /// 写完重启（repeated start）再读：寄存器读的标准动作。
  virtual Error i2c_write_read(std::uint8_t address7, etl::span<const std::uint8_t> write_data,
                               etl::span<std::uint8_t> read_data) noexcept = 0;

  /// 全双工一次：out 与 in 长度相同、同步收发（屏幕像素走这条）。
  virtual Error spi_transfer(etl::span<const std::uint8_t> out,
                             etl::span<std::uint8_t> in) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_BUS_H */
