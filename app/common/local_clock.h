/**
 * App 层的本地时间口径：固定东八区 + 一天秒数 + 公历换算 + 中文日期文案。
 *
 * 板子在国内用，先按固定东八区算；RTC 接上后由 C++ 侧统一处理时区，界面不管。
 * 放在公共头里是因为 launcher 与 clock 两个 App 都要把 epoch_ms 换算成本地时间
 * （一个写左上角小表 + 日期行、一个写大时钟 + 月历）—— 两处各写一份 8*3600
 * 或者各写一份 civil_from_days 迟早会对不上。
 */
#ifndef EMBARK_APP_LOCAL_CLOCK_H
#define EMBARK_APP_LOCAL_CLOCK_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace embark::demo {

inline constexpr std::int64_t kLocalOffsetSeconds = 8 * 3600;  // UTC+8
inline constexpr std::int64_t kSecondsPerDay = 86400;

/// 中文星期名（0 = 周日）—— 单字，界面上写成「星期六」或「周六」都取这一个表。
inline constexpr const char* kWeekdayShortCn[7] = {"日", "一", "二", "三", "四", "五", "六"};

/// 向下取整的除法（C++ 整除向零截断，负数时差一天）。
[[nodiscard]] inline std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
  const std::int64_t q = a / b;
  return ((a % b != 0) && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/// Howard Hinnant 的 civil-to-days：公历年月日 → 「1970-01-01 起的天数」。
[[nodiscard]] inline std::int64_t days_from_civil(int y, int m, int d) noexcept {
  const std::int64_t yy = (m <= 2) ? static_cast<std::int64_t>(y) - 1 : y;
  const std::int64_t era = floor_div(yy, 400);
  const std::int64_t yoe = yy - era * 400;                                  // [0, 399]
  const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
  const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
  return era * 146097 + doe - 719468;
}

/// 上面那个的反函数：天数 → 公历年月日。
inline void civil_from_days(std::int64_t z, int& y, int& m, int& d) noexcept {
  z += 719468;
  const std::int64_t era = floor_div(z, 146097);
  const std::int64_t doe = z - era * 146097;                                       // [0, 146096]
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  const std::int64_t yy = yoe + era * 400;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
  const std::int64_t mp = (5 * doy + 2) / 153;                       // [0, 11]
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
  y = static_cast<int>(yy + (m <= 2 ? 1 : 0));
}

/// 星期（0 = 周日）—— 1970-01-01 是周四，所以偏移 11。
[[nodiscard]] inline int weekday_sun0(std::int64_t days) noexcept {
  return static_cast<int>(((days % 7) + 11) % 7);
}

/**
 * 本地秒 → 「2026年10月17日 星期六」。
 *
 * 主屏日期行（launcher 屏的 L_Date）用它：设计稿初值是「2026年10月12日 星期一」同款格式，
 * 只是那天不会自己变。缓冲区给 48 字节：三个 %d 的理论上限各 11 字节，加上
 * 「年月日 星期」与 7 个字节的星期名刚好装得下。
 */
inline void format_local_date(std::int64_t local_seconds, char* out, std::size_t size) noexcept {
  const std::int64_t days = floor_div(local_seconds, kSecondsPerDay);
  int y = 0;
  int m = 0;
  int d = 0;
  civil_from_days(days, y, m, d);
  std::snprintf(out, size, "%d年%d月%d日 星期%s", y, m, d, kWeekdayShortCn[weekday_sun0(days)]);
}

}  // namespace embark::demo

#endif /* EMBARK_APP_LOCAL_CLOCK_H */
