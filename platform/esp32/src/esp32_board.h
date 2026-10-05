/**
 * Embark · ESP32-S3 板级参数（issues/11）
 *
 * 板子认的是 **Waveshare ESP32-S3-Touch-LCD-2.8**（模组 ESP32-S3R8：16 MB flash + 8 MB 八线 PSRAM）。
 * 下面每个数字都来自官方原理图 / 官方 wiki / 官方 demo，不是猜的：
 *   - 屏幕：ST7789（240×320 原生竖屏，SPI）；背光走 LEDC（13 位 / 5 kHz）。
 *   - 触摸：CST328（独立 I2C 总线，16 位寄存器地址，从地址 0x1A）。
 *   - 板载 I2C（IMU QMI8658 + RTC PCF85063）在另外两根脚上，框架的 hal::IBus 走那一条。
 *
 * 换板子只改这一个文件：显示/输入的引脚、面板原生尺寸、方向开关、内存预算都在这里。
 *
 * --- 实机 bring-up 时若方向不对，只动下面四个开关（各一行）---
 *   * 画面上下颠倒/左右镜像 → 改 lcd_mirror_x / lcd_mirror_y；
 *   * 触摸与画面错位（尤其是上下/左右反了）→ 改 touch_mirror_x / touch_mirror_y，
 *     让它与 LCD 的取值保持一致；
 *   * 触摸的 x/y 换了轴（点左边却跑到上边）→ 把 touch_swap_xy 置 true。
 * 判定依据：后端启动时会把 CST328 自己报的 RES_X / RES_Y 打进日志（官方驱动也是这么做的），
 * 面板真实分辨率一出来，轴方向就没有猜测空间了。
 */
#ifndef EMBARK_PLATFORM_ESP32_BOARD_H
#define EMBARK_PLATFORM_ESP32_BOARD_H

#include <cstddef>
#include <cstdint>

#include <embark_limits.h>

namespace embark::platform::esp32 {

// --- LCD：ST7789（SPI，只写不读）---------------------------------------------
inline constexpr int lcd_pin_mosi = 45;
inline constexpr int lcd_pin_sclk = 40;
inline constexpr int lcd_pin_cs = 42;
inline constexpr int lcd_pin_dc = 41;
inline constexpr int lcd_pin_rst = 39;
inline constexpr int lcd_pin_backlight = 5;
inline constexpr std::uint32_t lcd_pixel_clock_hz = 80U * 1000U * 1000U;  // 厂商驱动同值
inline constexpr std::uint16_t lcd_native_width = 240;                    // 面板原生 = 竖屏
inline constexpr std::uint16_t lcd_native_height = 320;
inline constexpr std::uint16_t lcd_offset_x = 0;
inline constexpr std::uint16_t lcd_offset_y = 0;

// 方向：正式口径是竖屏 240×320 —— 就是面板的原生方向，所以不做任何旋转
// （厂商旋转表的 ROT_NONE = swap_xy(false) + mirror(true,false)，与宿主窗口同向）。
// 实机若整屏转了 90° 或左右/上下镜像了，只翻这三个开关。
inline constexpr bool lcd_swap_xy = false;
inline constexpr bool lcd_mirror_x = true;
inline constexpr bool lcd_mirror_y = false;
// 面板需要反色（厂商初始化序列里有 0x21 INVON）—— 不反色的话画面会像底片。
inline constexpr bool lcd_invert_color = true;
// 颜色分量顺序：厂商 IDF demo 用 BGR（LCD_RGB_ENDIAN_BGR）⇒ MADCTL 的 BGR 位置 1。
// 实机若红蓝互换 → 把这个开关翻过来。
inline constexpr bool lcd_rgb_element_order_bgr = true;
// 16 位像素的字节序：厂商初始化写 0xB0=0x00,0xE8，其中 bit3 就是小端位 ⇒ 小端。
// 这正好与 LV_COLOR_16_SWAP=0 的 LVGL 缓冲（本机小端）一致，不必再搬一次字节。
// 实机若画面"发花/像把 16 位错开了一位"，第一个怀疑对象就是这一位。
inline constexpr bool lcd_data_little_endian = true;

// 背光：LEDC 低速模式 + 13 位分辨率 + 5 kHz（LED 不会闪、也不会啸叫）。
inline constexpr std::uint32_t backlight_pwm_hz = 5000;
inline constexpr int backlight_pwm_bits = 13;
inline constexpr std::uint8_t backlight_default_percent = 70;

// --- 触摸：CST328（独立 I2C，专用总线）--------------------------------------
inline constexpr int touch_pin_sda = 1;
inline constexpr int touch_pin_scl = 3;
inline constexpr int touch_pin_rst = 2;
inline constexpr int touch_pin_int = 4;
// 硬复位时序（厂商驱动：先拉低、再拉高，拉高之后留一段内部上电时间）。
inline constexpr std::uint32_t touch_reset_low_ms = 20;
inline constexpr std::uint32_t touch_reset_high_ms = 120;
inline constexpr std::uint32_t touch_i2c_hz = 400000;
// 触摸独占 I2C_NUM_0；板载 I2C（IMU/RTC）在 I2C_NUM_1 上 —— S3 有两个控制器，别接错。
inline constexpr int touch_i2c_port = 0;
inline constexpr std::uint8_t touch_address = 0x1A;
// 寄存器（16 位地址；协议细节见 esp32_input.cpp 的注释，照抄厂商驱动口径）
inline constexpr std::uint16_t touch_reg_count = 0xD005;  // 按下点数（低 4 位；写 0 清中断）
inline constexpr std::uint16_t touch_reg_points = 0xD000;      // 点坐标块，一次读 27 字节
inline constexpr std::uint16_t touch_reg_debug_mode = 0xD101;  // 调试信息模式
inline constexpr std::uint16_t touch_reg_normal_mode = 0xD109;  // 正常模式
inline constexpr std::uint16_t touch_reg_res_x = 0xD1F8;  // 面板 X 分辨率（调试寄存器）
inline constexpr std::uint16_t touch_reg_res_y = 0xD1FA;  // 面板 Y 分辨率
inline constexpr std::uint8_t touch_max_points = 5;
inline constexpr std::uint16_t touch_point_bytes = 27;  // 0xD000 一次读回的字节数
// 轮询周期：10 ms（100 Hz）。触摸不需要更快 —— CST328 自己也有内部滤波，
// 而每次读都是两次 I2C 事务，采样再密只会白占 CPU 与总线。
inline constexpr std::uint32_t touch_poll_period_ms = 10;
// CST328 原始坐标与面板同向（竖屏）：x 是短轴（0..239）、y 是长轴（0..319）。
// 实机若 x/y 互换 → 打开 touch_swap_xy；方向反了 → 打开对应的 touch_mirror_*。
// 启动日志会打印 CST328 自报的 RES_X / RES_Y，先用它判定轴方向再动这几个开关。
inline constexpr bool touch_swap_xy = false;
inline constexpr bool touch_mirror_x = false;
inline constexpr bool touch_mirror_y = false;

// --- 板载 I2C（IMU / RTC 共用）——框架 hal::IBus 走这条 ----------------------
inline constexpr int bus_pin_sda = 11;
inline constexpr int bus_pin_scl = 10;
inline constexpr std::uint32_t bus_i2c_hz = 400000;
// 板载 I2C 走 I2C_NUM_1（0 号口给触摸了）。
inline constexpr int bus_i2c_port = 1;
// 单次事务超时（毫秒）：400 kHz 下读 32 字节不到 1 ms，100 ms 已经等于"设备没应答"。
inline constexpr std::uint32_t bus_i2c_timeout_ms = 100;
// 这条总线上最多挂几个从设备（IMU + RTC + 以后要加的，4 个够；句柄是懒创建的）。
inline constexpr std::size_t bus_max_devices = 4;

// --- 任务落核 ----------------------------------------------------------------
// UI 任务落 core 1（IDF 自己的任务基本都在 core 0），后台 own task 落 core 0，
// 这样后台任务不会跟 UI 抢同一个核。
inline constexpr std::uint8_t ui_task_core = 1;
inline constexpr std::uint8_t own_task_core = 0;

// --- 内存预算 ----------------------------------------------------------------
// LVGL 的堆 = 一块 .bss 里的定容静态池（platform/esp32/src/esp32_lvgl_mem.cpp）。
// 宿主给的是 256 KB（那边是真实 malloc + 计数器），真机只有 SRAM，按实测收窄：
// 宿主演示跑满一轮的 LVGL 未回收量不到 10 KB，48 KB 对四个 demo App 绰绰有余。
inline constexpr std::size_t lvgl_pool_bytes = 48U * 1024U;

// 任务栈预算。框架里的 *_stack_words 是按**宿主的字长**调的（x86-64 的 StackType_t = size_t
// = 8 字节：2048 字 = 16 KB），而真机的 StackType_t 是 uint8_t（1 字 = 1 字节）——
// 照字面乘过去只剩 1/8：UI 任务只拿到 2 KB，栈指针会掉到栈缓冲**下面**，
// 先踩掉紧邻的 App 注册表与 efmt 的格式化上下文（.scratch/embark-v1/issues/21 的寄存器现场）。
// 所以真机按 stack_word_bytes 换算，让每个任务拿到**与宿主相同的字节数**；
// 要省 SRAM 就只动这一行，改完看心跳里的栈高水位（ui_task_stack_high_water_bytes）再收。
inline constexpr std::size_t stack_word_bytes = 8U;
inline constexpr std::size_t ui_task_stack_bytes = embark::ui_task_stack_words * stack_word_bytes;
inline constexpr std::size_t own_task_stack_bytes = embark::own_task_stack_words * stack_word_bytes;

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_BOARD_H */
