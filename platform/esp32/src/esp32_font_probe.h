/**
 * @file esp32_font_probe.h
 * @brief 字体运行时栅格化的真机侧测量入口
 *
 * 只在 `-DEMBARK_FONT_PROBE=ON` 时编进固件（见 platform/esp32/project/components/embark/
 * CMakeLists.txt 与 platform/esp32/project/CMakeLists.txt 的 option）。默认关闭 ⇒
 * esp32_font_probe.cpp 整个翻译单元是空的，main.cpp 里那句调用也不存在。
 *
 * 量的就是产品路径（桥建出来的那 7 个字号的实例），不另建实验字体，也不需要额外内存；
 * 它剩下的用途是「换字体 / 换字号 / 换缓存配额之后，真机上还是一样快吗」。
 */
#pragma once

namespace embark::platform::esp32::font_probe {

/// 跑一遍测量并把结果打到串口（[FONTPROBE] 前缀）。必须在 framework.boot() 之后调用：
/// 那时 LVGL 已初始化、SD 已挂载、UI 任务就是当前任务（LVGL 锁天然独占）。
void run();

}  // namespace embark::platform::esp32::font_probe
