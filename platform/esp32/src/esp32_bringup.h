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
 * 由 platform/esp32/src/esp32_board.h 的 bringup_self_test 开关控制（main.cpp 里两处
 * `if constexpr`）。真机接上 aidogui/EEZ UI 后端后把那个开关置 false，本对文件可整个删。
 *
 * 另有 check_sd_font()（issue 25）：SD 卡 + 运行时字库自检，与自检屏相互独立 —— 它不看
 * bringup_self_test 这个开关，因为"卡上有字库文件、能加载、能渲染、账目归零"正是 issue 25
 * 的验收证据（判读表见 platform/esp32/README.md §5）。
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

/// SD 卡 + 运行时字库自检（issue 25）：加载 sd_font_probe_file、量 sd_font_probe_text 的
/// 尺寸、真渲染一个字形，然后释放并核对记账与字库池归零。
///
/// 返回值 = 这一项到底过没过：只有「卡挂载 + 字库服务起来 + 文件加载 + 尺寸落在期望窗口 +
/// 渲染没炸 + 释放后记账归零」全过才是 true；没插卡、没配引脚、文件不在、字号不对、账目
/// 没归零都返回 false（"没插卡就当通过"等于这轮白测）。调用方 main.cpp 拿它决定发通过
/// 令牌还是 fatal。必须在 framework.boot()（lv_init）之后调用，只需调一次。
[[nodiscard]] bool check_sd_font();

}  // namespace embark::platform::esp32::bringup

#endif /* EMBARK_PLATFORM_ESP32_BRINGUP_H */
