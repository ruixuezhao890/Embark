/**
 * HAL · 显示（spec §8）
 *
 * 接口只认"一块矩形 + 一段像素字节"。屏幕上是什么内容、用不用 LVGL、怎么双缓冲，
 * 都是上层（UI 任务连同一个显示后端）的事，不是 HAL 的事。
 *
 * flush() 的契约：调用方保证 data 的格式就是 info().format、长度至少
 * area_bytes(area, format)；后端在这之前可以拆包（SPI 分包）、在这之后可以等 DMA —— 但
 * 返回时这块缓冲区必须已经可以复用（同步语义，v1 不暴露"异步 flush 完成回调"）。
 */
#ifndef EMBARK_HAL_DISPLAY_H
#define EMBARK_HAL_DISPLAY_H

#include <cstdint>

#include <embark/error.h>
#include <embark/hal/types.h>
#include <middleware/etl/span.h>

namespace embark::hal {

class IDisplay {
 public:
  virtual ~IDisplay() = default;

  /// 初始化面板与总线。失败要能重复调用（调用方可能重试）。
  virtual Error init() noexcept = 0;

  [[nodiscard]] virtual bool is_ready() const noexcept = 0;

  /// 面板参数（分辨率、像素格式、行跨度）。未初始化时返回零值结构。
  [[nodiscard]] virtual DisplayInfo info() const noexcept = 0;

  /// 把一块区域刷到屏幕上。
  virtual Error flush(const Rect& area, etl::span<const std::uint8_t> data) noexcept = 0;

  /// 背光亮度 0..100（百分比）。> 100 → invalid_argument；不支持背光 → unsupported。
  virtual Error set_backlight(std::uint8_t percent) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_DISPLAY_H */
