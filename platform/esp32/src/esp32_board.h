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
// 轮询周期：5 ms（200 Hz）。2026-10-17 从 10 ms 减半 —— 判抬手的依据已经换成报文里的
// 手指标志（见 esp32_input.cpp 的 read_point），不再靠"数空拍"，所以这个周期直接就是跟手
// 延迟本身，越快越跟手。每拍一次 I2C 读（29 字节，400 kHz 下约 0.8 ms），200 Hz 的开销可忽略。
inline constexpr std::uint32_t touch_poll_period_ms = 5;
// 去抖（2026-10-17 真机实测：一次按压被拆成几十次 press/release —— 10 秒里"触摸按下"从
// 56 次涨到 259 次，一次点击在天气屏上连切了 4 个城市）。机制是 CST328 在手指不动时会报
// "0 个触点"（那是"没有新报告"，不是"抬手"），驱动每拍读完又清中断，于是空拍被当成抬起、
// 下一拍又当成重新按下。
// 现在判据是 0xD000 报文里的**第 0 字节**（手指标志）：真机实测按住时恒为 0x06、抬手当即变
// 0x00，连"0xD005 读到 0"的那些拍也一直保持着 —— 抬起不必再等空窗，1 拍就够（5 ms）。
// （INT 脚那条路已排除：三种上下拉配置下 GPIO4 恒读 1，见 .scratch 里的探测记录。）
// 下面两个计数只用来挡单拍杂点（总线抖一下、状态切换那一拍里的半旧数据）：
//   * 按下 —— 连续 touch_press_confirm_hits 拍都"有手指"才算（2 拍 = 10 ms）；
//   * 抬起 —— 连续 touch_release_confirm_hits 拍都"没手指"才算（2 拍 = 10 ms）。
// 中间那些"还没确认"的拍被静默吞掉（计数见 Esp32Input::swallowed_gaps()），不抬也不重按。
// 对照：改之前抬起要等 6 拍（60 ms），按下到看见反应约 65 ms、松手到抬起约 75~105 ms。
inline constexpr std::uint8_t touch_press_confirm_hits = 2;
inline constexpr std::uint8_t touch_release_confirm_hits = 2;
// CST328 原始坐标与面板同向（竖屏）：x 是短轴（0..239）、y 是长轴（0..319）。
// 实机若 x/y 互换 → 打开 touch_swap_xy；方向反了 → 打开对应的 touch_mirror_*。
// 启动日志会打印 CST328 自报的 RES_X / RES_Y，先用它判定轴方向再动这几个开关。
inline constexpr bool touch_swap_xy = false;
inline constexpr bool touch_mirror_x = false;
inline constexpr bool touch_mirror_y = false;

// --- bring-up 自检屏 ----------------------------------------------------------
// 上板核对「方向 / 颜色 / 触摸」用的最小证据屏（platform/esp32/src/esp32_bringup.cpp）：
// 四角色标 TL/TR/BL/BR + R/G/B/W 四条纯色带 + 中央实时触摸坐标。
// 为什么曾默认开：2026-10-06 那些天真机还没有 UI 后端（app/common/ui_backend.h 的 #else
// 全是 no-op），烧上去本是一屏空白，bring-up 清单里的方向/颜色两项没有可比对的画面。
// 2026-10-17 起真机接上了 AiDoGUI UI 后端（components/embark_aidogui + EMBARK_AIDOGUI_UI），
// 默认屏就是 launcher 的真实界面 ⇒ 这张自检屏改回 false（它占着默认屏，会挡住界面）。
// 要重新核对方向/颜色/触摸时临时置 true，看完再关；彻底不用了就把 esp32_bringup.{h,cpp}
// 连同 main.cpp 的两处 if constexpr 一起删。
inline constexpr bool bringup_self_test = false;

// --- SD 卡（issue 25）----------------------------------------------------------
// 接线已量：卡座的四根 SPI 线走 IO14（SCLK）/ IO16（MISO）/ IO17（MOSI）/ IO21（CS）
// —— 微雪板 SD 引脚表逐脚核对过。IO15（D2）与 IO18（D1）是 4-bit SD 模式的数据线，
// 1-bit SPI 用不到，留空不接。
//
// 走 SPI 而不是 SDMMC：SPI 对引脚没有额外要求，任意四个 GPIO 都能接；SDMMC 要求卡座
// 的信号走在固定的 IO_MUX 引脚上。SPI 主机用 SPI2（SPI3 归屏幕，见 esp32_display.cpp），
// 驱动是 IDF 的 sdspi（fatfs 之上那一层与 SDMMC 共用）。
#define EMBARK_SD_ENABLED 1
inline constexpr bool sd_enabled = (EMBARK_SD_ENABLED != 0);
inline constexpr int sd_pin_cs = 21;    // IO21 → SD_CS
inline constexpr int sd_pin_mosi = 17;  // IO17 → SD_MOSI
inline constexpr int sd_pin_miso = 16;  // IO16 → SD_MISO
inline constexpr int sd_pin_clk = 14;   // IO14 → SD_SCLK
// 卡检测脚（卡座上的 CD）：-1 = 没接。填了就只在启动日志里报一行电平 —— 极性随卡座
// 而定，确认之前不拿它做判断（挂载失败同样能得出"没插卡"的结论）。
inline constexpr int sd_pin_detect = -1;
// 主机号是 spi_host_device_t 的**枚举值**，不是名字里的数字（IDF
// components/hal/include/hal/spi_types.h：SPI1_HOST=0、SPI2_HOST=1、SPI3_HOST=2）。
// 这里曾写 2 —— 那落到的正是屏幕占着的 SPI3，spi_bus_initialize 直接回
// ESP_ERR_INVALID_STATE（"SPI bus already initialized"），SD 挂不上。实机 2026-10-17 撞过。
inline constexpr int sd_spi_host = 1;  // = SPI2_HOST
// 10 MHz：杜邦线/长线也能跑稳；卡与线都短且质量好时可以往上加。
inline constexpr std::uint32_t sd_spi_hz = 10U * 1000U * 1000U;
inline constexpr const char* sd_mount_point = "/sdcard";
// 字库自检没过时的策略：默认**只报错、继续启动**（2026-10-17 改）。以前这里是 true —— 当场
// abort() 走 IDF panic handler 打回溯，是"专门验收这一项"时的一次性手段。设备到了日常使用，
// 卡没插好、文件被删、卡槽接触不良都会让它 abort → panic → 重启 → 再 abort，无限重启循环：
// 既看不到原因，也没法降级使用（字号退回内置 14 px 总比一直重启好）。要当场取证时再临时置 true。
//
// 自检本身在 main.cpp：ADR 0017 之后字体只有一条路 —— AiDoGUI 桥读卡上的 ui_heiti.ttf
// 建 7 档字号，所以判据就是桥的字体报告（loaded == total），不再自己读字库文件。
inline constexpr bool sd_font_check_fatal = false;

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

// --- 板载 RTC：PCF85063ATL（挂上面那条 I2C_NUM_1）-----------------------------
// 引脚（2026-10-17 用户按图纸确认，芯片已焊）：IO9 = RTC_INT / IO10 = SCL / IO11 = SDA
// —— SCL/SDA 与上面的 bus_pin_scl / bus_pin_sda 是同一对，所以 RTC 不是"另一条总线"，
// 而是 I2C_NUM_1 上的第二个从设备（第一个是 IMU QMI8658）。
inline constexpr std::uint8_t rtc_address = 0x51;
// 中断脚只记录、不驱动：本驱动开机读一次 + 之后用 tick 推算，不需要秒中断。
// 将来要做"每秒由 RTC 唤醒"时，把它接成 GPIO 中断再改 Esp32Time。
inline constexpr int rtc_pin_int = 9;
// 开机可以用"固件编译时刻"给 RTC 播种（2026-10-17 定的策略，见 esp32_time.cpp 的
// seed_from_build_time()）：板子没有可信时间源，但"这份固件是什么时候编出来的"是已知的，
// 用它把芯片从 2000-01-01 拉回现在、顺手清掉 OS 位。
// 误差 = 编译到上电那几分钟；没有纽扣电池时每次开机都会重新播种（这也是当前的实情）。
// 有电池之后播种一次就能一直走，那时把这个开关留着也无害（OS=0 就不会再写）。
inline constexpr bool rtc_seed_from_build_time = true;

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

// 字库区（issue 25 / ADR 0017）：字体的分配单独走一块 PSRAM 池 —— 与 LVGL 的 .bss
// 静态池（上面那个 lvgl_pool_bytes）分开，于是"建字体、栅格化字形"并不会推动
// lvgl_peak_bytes：字库的账与 LVGL 堆预算各管各的。
// 池是首次要用时才从 PSRAM 里显式分配出来的（heap_caps_malloc + MALLOC_CAP_SPIRAM，
// 见 esp32_lvgl_mem.cpp），没人建字体就不占这块内存。0 = 关掉（字库与 LVGL 挤同一个池）。
// 注意 TTF 字源本身不走这块池：3.85 MB 的整份字体用 font_buffer_alloc() 单独占 PSRAM。
// ADR 0017 起这块池装的是 TTF 的**字形缓存**（7 个字号各一份 LRU，见
// app/common/aidogui_ui_bridge.cpp 的 kCacheQuotaDefaults，实测定稿合计 118 KB）—— 每个缓存
// 字形在池里实占"16 字节对齐的位图 + 48 字节块头"，比 LRU 记的字节数大约三成，所以留到
// 192 KB。桥在 init 时按"池容量 × 3/4"收配额（fit_cache_quotas），所以池缩小也不会出现
// "配额之和 > 池"那种每帧重栅格化。
inline constexpr std::size_t font_psram_bytes = 192U * 1024U;

// 任务栈预算。框架里的 *_stack_words 是按**宿主的字长**调的（x86-64 的 StackType_t = size_t
// = 8 字节：2048 字 = 16 KB），而真机的 StackType_t 是 uint8_t（1 字 = 1 字节）——
// 照字面乘过去只剩 1/8：UI 任务只拿到 2 KB，栈指针会掉到栈缓冲**下面**，
// 先踩掉紧邻的 App 注册表与 efmt 的格式化上下文（.scratch/embark-v1/issues/21 的寄存器现场）。
// 所以真机按 stack_word_bytes 换算，让每个任务拿到**与宿主相同的字节数**；
// 要省 SRAM 就只动这一行，改完看心跳里的栈高水位（ui_task_stack_high_water_bytes）再收。
inline constexpr std::size_t stack_word_bytes = 8U;
inline constexpr std::size_t ui_task_stack_bytes = embark::ui_task_stack_words * stack_word_bytes;
inline constexpr std::size_t own_task_stack_bytes = embark::own_task_stack_words * stack_word_bytes;

// SD 启用（EMBARK_SD_ENABLED = 1）时四个信号脚必须都填好；卡检测脚可以继续留 -1。
static_assert(EMBARK_SD_ENABLED == 0 ||
                  (sd_pin_cs >= 0 && sd_pin_mosi >= 0 && sd_pin_miso >= 0 && sd_pin_clk >= 0),
              "SD 已启用（EMBARK_SD_ENABLED = 1）但信号脚还是 "
              "-1：先从原理图/图纸查出卡座接线，再打开这个开关");

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_BOARD_H */
