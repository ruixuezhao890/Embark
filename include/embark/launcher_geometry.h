/**
 * Embark · 启动器几何与槽位模型（issue 16）
 *
 * 这个头文件里只有"数学"：扇面几何、槽号换算、弹簧缓动、拖动换算，以及无图标
 * App 的首字兜底。它【不 include LVGL】—— 于是单元测试（embark_tests 不链 LVGL）
 * 能直接测这些边界，启动器与宿主验收脚本也能共用同一套坐标口径。
 *
 * 角度口径（自定，与 LVGL 弧的 0°=正右 不同，别混）：
 *   0° = 正上方（支点往上），正角度 = 顺时针（向右）；槽 i 的夹角 =
 *   (i - pos) × STEP，pos = "当前选中槽"的浮点位置，所以选中槽恒在 0°（正上方）。
 *
 * 常量全部来自 issue 16 / ADR 0006 的拍板值（240×320 竖屏）：
 *   支点 (120, 270)、半径 82、槽距 26°、拖动 1px ≈ 0.1 槽、弹簧 pos += (tgt-pos)×0.15。
 */
#ifndef EMBARK_LAUNCHER_GEOMETRY_H
#define EMBARK_LAUNCHER_GEOMETRY_H

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <embark_limits.h>

namespace embark::launcher {

/// 扇面支点（屏幕坐标，底部中心）。
inline constexpr float pivot_x = 120.0F;
inline constexpr float pivot_y = 270.0F;

/// 弧半径（px）。
inline constexpr float radius = 82.0F;

/// 相邻槽的夹角（度）。
inline constexpr float step_deg = 26.0F;

/// 最多几个槽 —— 与编译期注册表同容量（超了注册表本身就在编译期报错）。
inline constexpr std::size_t max_slots = max_apps;

/// 拖动换算：1px ≈ 0.1 槽（issue 16；参考 HTML 用的是 /60，以 issue 为准）。
inline constexpr float drag_px_per_slot = 10.0F;

/// 弹簧缓动系数与收敛阈值（issue 16：pos += (tgt-pos)*0.15，5 ms 一拍）。
inline constexpr float spring_factor = 0.15F;
inline constexpr float spring_epsilon = 0.01F;

/// 弧轨的两端：±90° = 与支点同高的左右两端（整个上半圆）。
inline constexpr float arc_limit_deg = 90.0F;

/// 屏幕坐标（像素，左上角原点）。用 float 是因为它直接来自三角计算。
struct Point {
  float x = 0.0F;
  float y = 0.0F;
};

/// 槽 index 相对"正上方"的夹角（度）。pos 是浮点，动画期间槽会连续滑动。
[[nodiscard]] inline float slot_offset(int index, float pos) noexcept {
  return (static_cast<float>(index) - pos) * step_deg;
}

/// 夹角 → 屏幕坐标。
[[nodiscard]] inline Point slot_center(float offset_deg) noexcept {
  constexpr float deg_to_rad = 3.14159265358979323846F / 180.0F;
  const float radians = offset_deg * deg_to_rad;
  return Point{pivot_x + radius * std::sin(radians), pivot_y - radius * std::cos(radians)};
}

/// 便捷：槽 index 在"选中槽 = selected"时的屏幕坐标（宿主合成点击的锚点）。
[[nodiscard]] inline Point slot_center_for(int index, int selected) noexcept {
  return slot_center(slot_offset(index, static_cast<float>(selected)));
}

/// 把任意槽号折回 [0, count)（负数也正确）。
[[nodiscard]] inline int normalize_index(int index, int count) noexcept {
  if (count <= 0) {
    return 0;
  }
  const int value = index % count;
  return value < 0 ? value + count : value;
}

/// 离"正上方"最近的槽号（= 被选中的槽）。
[[nodiscard]] inline int selected_index(float pos, int count) noexcept {
  if (count <= 0) {
    return 0;
  }
  return normalize_index(static_cast<int>(std::lround(pos)), count);
}

/// 夹角是不是"选中槽"的位置（命中测试与视觉高亮都用它）。
[[nodiscard]] inline bool is_selected(float offset_deg) noexcept {
  return std::fabs(offset_deg) < step_deg * 0.5F;
}

/// 弹簧目标夹到 [0, count-1]：扇面不绕圈，两端就是头尾两个槽。
[[nodiscard]] inline float clamp_target(float pos, int count) noexcept {
  if (count <= 0) {
    return 0.0F;
  }
  const float last = static_cast<float>(count - 1);
  if (pos < 0.0F) {
    return 0.0F;
  }
  return pos > last ? last : pos;
}

/// 拖动换算：向下拖（dy > 0）→ 扇面顺时针转 → 槽号变小。
/// （抓住正上方那个槽往下拽，左边那个槽会被带到正上方来。）
[[nodiscard]] inline float drag_target(float start_pos, float dy_px, int count) noexcept {
  return clamp_target(start_pos - dy_px / drag_px_per_slot, count);
}

/// 弹簧走一步。
[[nodiscard]] inline float spring_step(float pos, float target) noexcept {
  return pos + (target - pos) * spring_factor;
}

/// 弹簧是否已经收敛（|pos-tgt| < 0.01）。
[[nodiscard]] inline bool spring_settled(float pos, float target) noexcept {
  return std::fabs(target - pos) < spring_epsilon;
}

/// 取 UTF-8 串的首字符（1..4 字节）写进 out（含结尾 NUL）。
/// 无图标 App 的槽位图标 = 标题首字，所以必须按码点切，不能只取一个字节
/// （截断的 UTF-8 在 LVGL 里是乱码或缺字）。返回 false = 空串/非法序列/缓冲不够。
[[nodiscard]] inline bool first_utf8_char(const char* text, char* out, std::size_t capacity) noexcept {
  if (text == nullptr || out == nullptr || capacity == 0U) {
    return false;
  }
  const auto lead = static_cast<unsigned char>(text[0]);
  if (lead == 0U) {
    return false;
  }
  std::size_t length = 0U;
  if ((lead & 0x80U) == 0U) {
    length = 1U;
  } else if ((lead & 0xE0U) == 0xC0U) {
    length = 2U;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3U;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4U;
  } else {
    return false;  // 续字节或非法首字节
  }
  if (length + 1U > capacity) {
    return false;
  }
  for (std::size_t index = 1U; index < length; ++index) {
    if (text[index] == '\0' || (static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < length; ++index) {
    out[index] = text[index];
  }
  out[length] = '\0';
  return true;
}

}  // namespace embark::launcher

#endif /* EMBARK_LAUNCHER_GEOMETRY_H */
