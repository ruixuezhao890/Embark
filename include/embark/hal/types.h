/**
 * Embark · HAL 公共类型（spec §8）
 *
 * 这里的每个类型都必须与芯片无关：不出现引脚、总线参数、屏幕型号。
 * 坐标与尺寸用 int32 —— 区域计算会做加减，用窄类型反而要在每处担心溢出。
 *
 * 打印（issue 13）：本文件的枚举与结构体都用 efmt 的派生宏声明（E_FMT_DERIVE /
 * E_FMT_DERIVE_ENUM），字段名与取值名在编译期从声明文本里推出来，所以日志里可以
 * 直接打整个对象，也可以随手加字段（没有需要同步的名字表）：
 *
 *   ELOG_INFO("显示 {}，区域 {}", display_info, area);
 *
 * 少数需要 const char* 的出口（fprintf / fatal）用 e_fmt::format_to 写到栈上缓冲。
 * 约束：一个字段一行（宏的第一个参数是声明本身，声明里的顶层逗号会切断宏参数）。
 */
#ifndef EMBARK_HAL_TYPES_H
#define EMBARK_HAL_TYPES_H

#include <cstddef>
#include <cstdint>

#include <middleware/efmt/core/format.hpp>

namespace embark::hal {

/// 屏幕区域（像素，左上角原点）。
E_FMT_DERIVE(struct Rect {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
});

/// 区域非空才值得刷。
constexpr bool is_valid(const Rect& area) noexcept {
  return area.width > 0 && area.height > 0;
}

/// 像素格式：显式写出来，别让上下层各自猜（LVGL 的 lv_color_t 与屏幕格式不一致时要转）。
E_FMT_DERIVE_ENUM(enum class PixelFormat
                  : std::uint8_t{
                      mono1,     ///< 1bpp
                      grey4,     ///< 4bpp 灰度
                      rgb332,    ///< 8bpp
                      rgb565,    ///< 16bpp（ST7789 与宿主 SDL 纹理都用它）
                      rgb888,    ///< 24bpp
                      argb8888,  ///< 32bpp 带 alpha
                  });

/// 每像素位数（算区域字节数用）。
constexpr std::uint8_t bits_per_pixel(PixelFormat format) noexcept {
  switch (format) {
    case PixelFormat::mono1:
      return 1;
    case PixelFormat::grey4:
      return 4;
    case PixelFormat::rgb332:
      return 8;
    case PixelFormat::rgb565:
      return 16;
    case PixelFormat::rgb888:
      return 24;
    case PixelFormat::argb8888:
      return 32;
  }
  return 0;
}

/// 一块区域需要的字节数（单色按整字节向上取整；实际后端还会各自对齐）。
constexpr std::size_t area_bytes(const Rect& area, PixelFormat format) noexcept {
  const std::size_t pixels =
      static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height);
  return (pixels * bits_per_pixel(format) + 7U) / 8U;
}

E_FMT_DERIVE(struct DisplayInfo {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  PixelFormat format = PixelFormat::rgb565;
  std::uint16_t stride_bytes = 0;  ///< 一行的字节数；0 表示 = width × 每像素字节数
});

E_FMT_DERIVE_ENUM(enum class InputEventKind
                  : std::uint8_t{
                      press,
                      release,
                      move,
                  });

/// 一次输入事件。
///
/// 约定（issue 05 明确）：**指针事件的 key 恒为 0**（触摸、鼠标、触控板都是"按下/抬起/移动"），
/// **按键事件的 key 非 0**（键号 0..255），此时 x/y 记的是"按键那一刻指针在哪"，仅供参考。
/// 上层因此只看 key 就能把两类事件分开；宿主后端把鼠标与键盘事件都塞进同一支队列，
/// 这条约定就是它们的交界（宿主鼠标的 button 号 v1 用不上，不占 key）。
E_FMT_DERIVE(struct InputEvent {
  InputEventKind kind = InputEventKind::press;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t key = 0;  ///< 0 = 指针事件；非 0 = 按键号（2 字节，见 message.h 的说明）
  std::uint32_t timestamp_ms = 0;  ///< 事件发生时刻（走 HAL 时间轴）
});

}  // namespace embark::hal

#endif /* EMBARK_HAL_TYPES_H */
