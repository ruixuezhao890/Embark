/**
 * Embark 版本信息
 *
 * 版本号在顶层 CMakeLists.txt 的 project(embark VERSION ...) 里定义；
 * 0.x 期间不承诺 API 兼容（spec §12）。
 */
#ifndef EMBARK_VERSION_H
#define EMBARK_VERSION_H

#define EMBARK_VERSION_MAJOR 0
#define EMBARK_VERSION_MINOR 1
#define EMBARK_VERSION_PATCH 0
#define EMBARK_VERSION_STRING "0.1.0"

namespace embark {

// 版本字符串（静态存储，不分配）
const char* version_string();

// 当前构建的平台名："host"（PC 仿真）或 "esp32"
const char* platform_name();

}  // namespace embark

#endif /* EMBARK_VERSION_H */
