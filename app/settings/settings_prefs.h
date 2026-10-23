/**
 * Embark · 设置项的偏好键与读写（header-only，零堆）。
 *
 * 为什么不是 app/common：今天只有一个消费者（SettingsApp）。等第二个 App 也要读同一批偏好
 * 再往上提 —— 现在提就是为一个假想的读者留接口。
 *
 * 键统一 ui. 前缀（ADR 0018 决策②）：以后会有 ui.* 一把，把它们收在同一个命名空间里，
 * 一眼能看出"这是用户可见的偏好，不是设备校准值"。键长与值长远在 docs/reference/limits.md
 * 的 16 B / 64 B 之内（最长的 ui.bright 是 9 B，值恒为 1 B）。
 *
 * 读写都照 app/weather/weather_data.h 的 weather_load_city / weather_save_city：
 * 读失败当没存过（回默认值），写失败当没存上（这次会话照常调，下次启动回默认）——
 * 存不下不该把设置界面变成错误弹窗。
 */
#ifndef EMBARK_APP_SETTINGS_SETTINGS_PREFS_H
#define EMBARK_APP_SETTINGS_SETTINGS_PREFS_H

#include <cstdint>

#include <embark/hal/persistence.h>
#include <middleware/etl/span.h>

namespace embark::demo {

/// 背光百分比的 KV 键：一个字节，0..100。
inline constexpr const char* kBrightKey = "ui.bright";

/// 没存过时的背光百分比。板级 backlight_default_percent 也是 70（device/src/esp32_board.h），
/// 这里再写一份是因为"App 持有亮度镜像"：KV 空 → 用这个值 → 立刻写进 HAL，
/// 于是"开机亮度"只有一个可改的地方（这个常量），不会被两处默认值扯成不一致。
inline constexpr std::uint8_t kBrightDefaultPercent = 70U;

/// 亮度按钮的步进：+/− 各 10 个百分点。
inline constexpr int kBrightStep = 10;

/// 钳到 0..100（两端都要钳：0 以下不能变成 246）。
[[nodiscard]] inline std::uint8_t clamp_percent(int percent) noexcept {
  if (percent < 0)
    return 0U;
  if (percent > 100)
    return 100U;
  return static_cast<std::uint8_t>(percent);
}

/// 读已保存的背光百分比：没存过 / 读失败 / 越界（>100）→ kBrightDefaultPercent。
inline std::uint8_t load_bright(embark::hal::IPersistence& storage) noexcept {
  std::uint8_t raw = 0U;
  const auto got = storage.read(kBrightKey, etl::span<std::uint8_t>(&raw, 1U));
  if (!got.has_value() || got.value() != 1U || raw > 100U)
    return kBrightDefaultPercent;
  return raw;
}

/// 保存背光百分比（失败只当没存上）。
inline void save_bright(embark::hal::IPersistence& storage, std::uint8_t percent) noexcept {
  const std::uint8_t raw = clamp_percent(static_cast<int>(percent));
  (void)storage.write(kBrightKey, etl::span<const std::uint8_t>(&raw, 1U));
}

}  // namespace embark::demo

#endif /* EMBARK_APP_SETTINGS_SETTINGS_PREFS_H */