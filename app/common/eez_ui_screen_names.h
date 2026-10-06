/**
 * EEZ 屏名 → App 名解析（纯函数：不 include LVGL，也不 include Embark）。
 *
 * 屏名约定（用户口径，2026-10-06 起）：EEZ Studio 里 screen 名 == App 名；
 * App 的子页面命名 <app名>_<编号>_sub（编号 = 子页序号，从 1 起）。
 *   - "launcher"      → App "launcher"
 *   - "clock_1_sub"   → App "clock"      （子页归到它的 App）
 *   - "ticker_2_3_sub" → App "ticker_2"  （只剥最后一段 _<编号>_sub）
 *   - "clock_1"       → "clock_1"        （不是子页形状，原样）
 *
 * 解析放在这里、不放在桥里，是为了能脱离 LVGL/生成代码单测（tests/kernel/
 * test_eez_screen_names.cpp）；桥与导航层（eez_ui_bridge / eez_ui_nav）调它。
 * 约定如有变动，只需改这一处 + 对应测试。
 */
#ifndef EMBARK_APP_EEZ_UI_SCREEN_NAMES_H
#define EMBARK_APP_EEZ_UI_SCREEN_NAMES_H

#include <cstddef>
#include <cstring>

namespace embark::demo {

/// App 名缓冲建议大小（现有 App 名 < 16 字节，留足余量）。
inline constexpr std::size_t eez_app_name_max = 32;

namespace detail_screen_names {

inline bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }

/// 子页 `<基名>_<数字>_sub` 里基名的长度；不是子页形状返回 0。
/// 只认最后一段：要求以 "_sub" 结尾、紧邻至少一位数字、数字前是 '_'，且基名非空。
inline std::size_t subpage_base_length(const char* name) noexcept {
  if (name == nullptr) {
    return 0;
  }
  const std::size_t length = std::strlen(name);
  if (length < 6U) {  // 最短 "a_1_sub"
    return 0;
  }
  if (std::strcmp(name + (length - 4U), "_sub") != 0) {
    return 0;
  }
  std::size_t cursor = length - 4U;  // 指向 "_sub" 的那个 '_'
  std::size_t digits = 0;
  while (cursor > 0U && is_ascii_digit(name[cursor - 1U])) {
    --cursor;
    ++digits;
  }
  if (digits == 0U || cursor == 0U || name[cursor - 1U] != '_') {
    return 0;
  }
  return cursor - 1U;  // 基名长度（"_1_sub" 这种基名为空的写法返回 0 = 不算子页）
}

}  // namespace detail_screen_names

/// 屏名是不是 `<...>_<数字>_sub` 形状（App 的子页面）。
inline bool eez_ui_screen_is_subpage(const char* screen_name) noexcept {
  return detail_screen_names::subpage_base_length(screen_name) > 0U;
}

/// 屏名 → App 名候选，写进 buffer（总以空字符收尾），返回 buffer。
/// 参数非法 / 结果为空 / buffer 装不下时返回 nullptr（调用方按"没有对应 App"处理）。
inline const char* eez_ui_app_name_for_screen(const char* screen_name, char* buffer,
                                             std::size_t size) noexcept {
  if (screen_name == nullptr || buffer == nullptr || size == 0U) {
    return nullptr;
  }
  const std::size_t base = detail_screen_names::subpage_base_length(screen_name);
  const std::size_t length = (base > 0U) ? base : std::strlen(screen_name);
  if (length == 0U || length + 1U > size) {
    return nullptr;
  }
  std::memcpy(buffer, screen_name, length);
  buffer[length] = 0;
  return buffer;
}

}  // namespace embark::demo

#endif /* EMBARK_APP_EEZ_UI_SCREEN_NAMES_H */
