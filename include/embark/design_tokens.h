/**
 * Embark · 设计令牌（issue 16、ADR 0006）
 *
 * 手写 LVGL 的唯一取色来源：颜色/尺寸不写字面量，一律从这里取（ADR 0006 决策③）。
 * 本头【不 include LVGL】：embark_core 不链 LVGL（src/CMakeLists.txt），而 App 元数据
 * 界面与主题相关都要用到令牌，所以令牌是纯 C++ 常量，LVGL 侧自己用 lv_color_hex() 转。
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

/// 次级说明（未选中槽标题、辅助元素）。
inline constexpr std::uint32_t text_secondary = 0x8FA0B5U;

/// 强调色：选中态、强调元素。
inline constexpr std::uint32_t accent = 0x39D0C4U;

/// 虚线弧轨。
inline constexpr std::uint32_t rail = 0x2B3A4DU;

/// 未选中圆描边。
inline constexpr std::uint32_t ring = 0x41536BU;

/// 强调色之上的深色前景（选中槽反色时的图标/文字）。用底色而不是纯黑：反色块
/// 与背景同族，屏幕上不出现"挖洞"感。
inline constexpr std::uint32_t accent_contrast = bg;

/// 启动器 App 磁贴底色（issue launcher-pages；与 AiDoGUI 设计稿 ui_launcher 的磁贴一致）。
/// 磁贴是“深蓝底 + 浅蓝字”的一族，与面板 panel 不是同一个色，所以单列令牌。
inline constexpr std::uint32_t tile_bg = 0x202B47U;

/// 启动器 App 磁贴文字。
inline constexpr std::uint32_t tile_text = 0xC7D0E6U;

/// 启动器页码点：非当前页（与设计稿 ui_launcher 的 Dot2/Dot3 一致）。
inline constexpr std::uint32_t page_dot = 0x2A3550U;

/// 启动器页码点：当前页（设计稿 Dot1）。翻页时由 launcher_tiles 重涂。
inline constexpr std::uint32_t page_dot_active = 0x6EA0FFU;

/// 面板圆角（px）。
inline constexpr std::uint16_t radius = 8U;

/// 启动器磁贴圆角（px）：磁贴比面板小，圆角相对更大一点才不显方。
inline constexpr std::uint16_t tile_radius = 10U;

/// 旧导航壳状态行高（px；导航壳已退役，令牌保留以防屏上需要）。
inline constexpr std::uint16_t status_h = 28U;

}  // namespace embark::design_tokens

#endif /* EMBARK_DESIGN_TOKENS_H */
