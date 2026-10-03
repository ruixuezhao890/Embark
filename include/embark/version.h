/**
 * Embark 版本信息
 *
 * 版本号只有一处事实来源：顶层 CMakeLists.txt 的 `project(embark VERSION ...)`。
 * 它由 src/CMakeLists.txt 作为编译定义注入（EMBARK_VERSION_STRING），这里**不**再写一遍 ——
 * 头文件里再放一份，早晚会和构建系统对不上，而且没人会发现。
 *
 * 0.x 期间不承诺 API 兼容（spec §12）。
 */
#ifndef EMBARK_VERSION_H
#define EMBARK_VERSION_H

namespace embark {

// 版本字符串（静态存储，不分配）。形如 "0.1.0"。
// 没有经过构建系统（脱离 CMake 的 IDE 索引、手写编译命令）时返回兜底值 "0.0.0-unknown"，
// 冒烟测试会把这个兜底值当成失败 —— 让"漏传编译定义"变成看得见的事故。
const char* version_string();

// 当前构建的平台名："host"（PC 仿真）或 "esp32"；构建系统没传时为 "unknown"。
const char* platform_name();

}  // namespace embark

#endif /* EMBARK_VERSION_H */
