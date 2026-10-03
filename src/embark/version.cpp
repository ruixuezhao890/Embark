#include <embark/version.h>

// 构建系统没给平台名/版本号时不要静默：宁可打出兜底值让冒烟测试抓住
#ifndef EMBARK_PLATFORM_NAME
#define EMBARK_PLATFORM_NAME "unknown"
#endif

#ifndef EMBARK_VERSION_STRING
#define EMBARK_VERSION_STRING "0.0.0-unknown"
#endif

namespace embark {

const char* version_string() { return EMBARK_VERSION_STRING; }

const char* platform_name() { return EMBARK_PLATFORM_NAME; }

}  // namespace embark
