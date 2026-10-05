/**
 * Embark · 设计令牌（issue 16、ADR 0006）
 *
 * 手写 LVGL 的唯一取色来源：颜色/尺寸不写字面量，一律从这里取（ADR 0006 决策③）。
 * 本头【不 include LVGL】：embark_core 不链 LVGL（src/CMakeLists.txt），而 App 元数据
 * 与导航壳状态都要用到令牌，所以令牌是纯 C++ 常量，LVGL 侧自己用 lv_color_hex() 转。
 *
 * 颜色是 0xRRGGBB（LVGL 的 lv_color_hex 口径），不是预乘/带 alpha 的值。
 * 深色科技风首版，取值与 .scratch/embark-v1/issues/16-launcher-fan-and-nav.md 的表一致。
 */
#ifndef EMBARK_DESIGN_TOKENS_H
#define EMBARK_DESIGN_TOKENS_H

#include <cstdint>

namespace embark::design_tokens {

/// 全局底色（近黑蓝）。
inline constexpr std::uint32_t bg = 0x0E141BU;

/// 面板/按钮底。
inline constexpr std::uint32_t panel = 0x16212EU;

/// 标题、正文。
inline constexpr std::uint32_t text_primary = 0xE8EEF7U;

/// 次级说明（未选中槽标题、状态行）。
inline constexpr std::uint32_t text_secondary = 0x8FA0B5U;

/// 强调色：选中弧、选中槽填充、返回键。
inline constexpr std::uint32_t accent = 0x39D0C4U;

/// 虚线弧轨。
inline constexpr std::uint32_t rail = 0x2B3A4DU;

/// 未选中圆描边。
inline constexpr std::uint32_t ring = 0x41536BU;

/// 强调色之上的深色前景（选中槽反色时的图标/文字）。用底色而不是纯黑：反色块
/// 与背景同族，屏幕上不出现"挖洞"感。
inline constexpr std::uint32_t accent_contrast = bg;

/// 面板圆角（px）。
inline constexpr std::uint16_t radius = 8U;

/// 导航壳状态行高（px）。
inline constexpr std::uint16_t status_h = 28U;

}  // namespace embark::design_tokens

#endif /* EMBARK_DESIGN_TOKENS_H */
