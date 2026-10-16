/**
 * Embark · 天气数据桩（编译期常量，零堆零 IO）
 *
 * 框架当前**没有任何网络能力**（HAL 只有 time/persistence/log/system/bus/display/input；
 * WiFi 在 ADR 0004/0005 里还只是 own_task 策略的"将来用例"），所以最近 5 天的天气是
 * **编译期写死的桩**，不是真数据。这条决策记在 docs/adr/0012-weather-data-source.md。
 *
 * 谁在用：WeatherApp（天气屏）与 LauncherApp（主屏上的天气卡）读同一张表 —— 天气卡不是
 * 第二份数据，只是同一份数据的另一种画法（ADR 0011 的"数据接口走控件表按名查找"）。
 *
 * 字符串纪律（**改这里的任何字面量都要同步 screen.aid 的 i18n.customCharsetRows**）：
 * 设计器按 (字体, 字号) 子集化字形，字符集 = 设计期文字 ∪ 运行期字符。本表用到的字符
 * 必须出现在对应字号的 symbols 里，否则运行时是方框（glyph dsc not found）。字号分工：
 *   - 14px：城市名（L_City / 天气卡的 L_WCity）、屏标题、逐日天气字（晴/多云/…）
 *   - 12px：逐日星期标签（今天/周六…）、温度区间（18°/26°）
 *   - 11px：一句话描述（空气优 / 微风 / 有雨…）
 *   - 22px：天气屏「现在」区的大天气字；28px：大温度
 *
 * 零堆：只有字符串字面量与 POD 结构，没有任何动态分配。
 */
#ifndef EMBARK_APP_WEATHER_WEATHER_DATA_H
#define EMBARK_APP_WEATHER_WEATHER_DATA_H

#include <cstddef>
#include <cstdint>

#include <embark/hal/persistence.h>
#include <middleware/etl/span.h>
#include <middleware/etl/string_view.h>

namespace embark::demo {

/// 一天要画进界面的四段文字（控件名见 weather_app.cpp）。
struct WeatherDay {
  const char* dow;    // 12px：今天 / 周六 / 周日 …
  const char* icon;   // 14px：晴 / 多云 / 阴 / 小雨 / 中雨 / 大雨 / 雷雨
  const char* desc;   // 11px：空气优 / 空气良 / 微风 / 有雨 / 闷热
  const char* range;  // 12px：18°/26°
};

/// 一个城市：名字 + 当前温度（"现在"区用）+ 5 天（index 0 == 今天）。
struct WeatherCity {
  const char* name;  // 14px：北京 / 上海 / 广州 / 深圳
  const char* temp;  // 28px：26°
  WeatherDay days[5];
};

inline constexpr std::size_t kWeatherCityCount = 4;
inline constexpr std::size_t kWeatherDayCount = 5;

/// 持久化键（平台无关字符串）：存一个字节的城市下标。
inline constexpr const char* kWeatherCityKey = "wx.city";

/// 城市表（函数内 static，避免头文件里的外部链接变量；inline 函数保证单一定义）。
inline const WeatherCity* weather_cities() {
  static constexpr WeatherCity kCities[kWeatherCityCount] = {
      {"北京",
       "26°",
       {{"今天", "晴", "空气优", "18°/26°"},
        {"周六", "多云", "微风", "17°/25°"},
        {"周日", "阴", "有雨", "16°/23°"},
        {"周一", "小雨", "有雨", "15°/21°"},
        {"周二", "晴", "空气良", "16°/24°"}}},
      {"上海",
       "23°",
       {{"今天", "阴", "微风", "20°/25°"},
        {"周六", "小雨", "有雨", "19°/23°"},
        {"周日", "中雨", "有雨", "18°/22°"},
        {"周一", "多云", "空气良", "19°/24°"},
        {"周二", "晴", "空气优", "20°/26°"}}},
      {"广州",
       "30°",
       {{"今天", "雷雨", "闷热", "26°/33°"},
        {"周六", "大雨", "有雨", "25°/31°"},
        {"周日", "中雨", "有雨", "25°/30°"},
        {"周一", "多云", "闷热", "26°/32°"},
        {"周二", "晴", "闷热", "27°/34°"}}},
      {"深圳",
       "29°",
       {{"今天", "多云", "闷热", "27°/32°"},
        {"周六", "雷雨", "有雨", "26°/31°"},
        {"周日", "大雨", "有雨", "25°/30°"},
        {"周一", "阴", "微风", "26°/31°"},
        {"周二", "晴", "空气优", "27°/33°"}}},
  };
  return kCities;
}

/// 越界一律回落到第 0 个（城市是桩，不做错误分支）。
inline const WeatherCity& weather_city(std::size_t index) {
  return weather_cities()[index < kWeatherCityCount ? index : 0U];
}

/// 环形切换城市：delta = ±1（左右箭头）。
inline std::size_t weather_shift_city(std::size_t index, int delta) {
  const int n = static_cast<int>(kWeatherCityCount);
  int next = (static_cast<int>(index) + delta) % n;
  if (next < 0)
    next += n;
  return static_cast<std::size_t>(next);
}

/// 读已保存的城市下标：没存过 / 读失败 / 越界 → 0（第一个城市）。
inline std::size_t weather_load_city(embark::hal::IPersistence& storage) {
  std::uint8_t raw = 0U;
  const auto got = storage.read(kWeatherCityKey, etl::span<std::uint8_t>(&raw, 1U));
  if (!got.has_value() || got.value() != 1U || raw >= kWeatherCityCount)
    return 0U;
  return static_cast<std::size_t>(raw);
}

/// 保存城市下标（失败只当没存上：下次启动回到北京，不影响当前会话）。
inline void weather_save_city(embark::hal::IPersistence& storage, std::size_t index) {
  const std::uint8_t raw = static_cast<std::uint8_t>(index < kWeatherCityCount ? index : 0U);
  (void)storage.write(kWeatherCityKey, etl::span<const std::uint8_t>(&raw, 1U));
}

}  // namespace embark::demo

#endif /* EMBARK_APP_WEATHER_WEATHER_DATA_H */
