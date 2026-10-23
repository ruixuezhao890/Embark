/**
 * @file esp32_bringup.h
 * @brief 真机 bring-up 自检屏（B 阶段上板核对：方向 / 颜色 / 触摸）
 *
 * 为什么需要它：真机现在**没有 UI 后端**（app/common/ui_backend.h 的 #else 分支全是 no-op，
 * kUiBackendName = "none"），框架起来后屏上是 LVGL 的默认主题底 —— 一屏空白，bring-up
 * 清单里的「方向」「颜色」两项没有任何可比对的画面，触摸也只有输入后端里的按下计数。
 * 本屏用原生 LVGL 画最小证据集，不依赖任何后端代码：
 *   - 四角色标 TL/TR/BL/BR：整屏转了 90° 或上下/左右镜像，一眼就能看出来；
 *   - 四条纯色带 R/G/B/W：红蓝互换（BGR）、反色、16 位字节序错位各自有特征；
 *   - 中央实时坐标 + 按下沿串口日志：触摸 swap/mirror 直接读数判定，不靠猜。
 *
 * 由 device/src/esp32_board.h 的 bringup_self_test 开关控制（main.cpp 里两处
 * `if constexpr`）。真机接上 aidogui UI 后端后把那个开关置 false，本对文件可整个删。
 *
 * 这里的 check_sd_font()（SD 卡 + 运行时字库自检，issue 25）已在 issue 04 退役：字体改由
 * AiDoGUI 桥读卡上的 ui_heiti.ttf 建字号（ADR 0017），判据搬到了 main.cpp 直接看桥的字体
 * 报告；判读表仍见 device/README.md §5。
 */
#ifndef EMBARK_PLATFORM_ESP32_BRINGUP_H
#define EMBARK_PLATFORM_ESP32_BRINGUP_H

#include <cstdint>

namespace embark::platform::esp32::bringup {

/// 画出自检屏并把它设为当前屏；重复调用只生效一次（幂等）。
void show();

/// UI 循环每帧调用一次：读 LVGL 输入设备的最新坐标（= UI 真正收到的坐标），
/// 刷新屏上读数，并在按下沿打一条串口日志。show() 之前调用是 no-op。
void pump_touch();

}  // namespace embark::platform::esp32::bringup

#endif /* EMBARK_PLATFORM_ESP32_BRINGUP_H */
