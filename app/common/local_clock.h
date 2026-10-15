/**
 * App 层的本地时间口径：固定东八区 + 一天秒数。
 *
 * 板子在国内用，先按固定东八区算；RTC 接上后由 C++ 侧统一处理时区，界面不管。
 * 放在公共头里是因为 launcher 与 clock 两个 App 都要把 epoch_ms 换算成本地时间
 * （一个写左上角小表、一个写大时钟）—— 两处各写一份 8*3600 迟早会对不上。
 */
#ifndef EMBARK_APP_LOCAL_CLOCK_H
#define EMBARK_APP_LOCAL_CLOCK_H

#include <cstdint>

namespace embark::demo {

inline constexpr std::int64_t kLocalOffsetSeconds = 8 * 3600;  // UTC+8
inline constexpr std::int64_t kSecondsPerDay = 86400;

}  // namespace embark::demo

#endif /* EMBARK_APP_LOCAL_CLOCK_H */
