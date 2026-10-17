/**
 * @file lv_conf.h
 * @brief LVGL 8.3.11 的工程内配置（自持，不依赖上游示例）。
 *
 * 约定（issue 05）：
 *  - 全仓库只有这一份 LVGL 配置：宿主（SDL2 窗口）与目标（ST7789，issue 11）共用；
 *    颜色深度固定 16 位 RGB565，与显示 HAL 的 PixelFormat::rgb565 一致。
 *  - 只钉住会影响「内存、尺寸、构图、可观测性」的项；其余（widget 开关、主题细节）
 *    交给 lv_conf_internal.h 的 #ifndef 默认，避免每次升 LVGL 都要重抄 600 行模板。
 *  - 本文件被 LVGL 的 C 源码包含，必须是合法 C 头：不放 C++ 语法。
 *
 * 改这里的任何一项都要重跑宿主 UI 目标（embark_host_ui）并复核 lv_conf_internal.h
 * 里同名项的默认值，确认我们没有悄悄依赖默认。
 *
 * 头文件护栏必须叫 LV_CONF_H：lv_conf_internal.h:43 用 `#if !defined(LV_CONF_H)` 判断
 * "配置到底有没有被包含进来"，护栏名写错会在每个翻译单元刷一条 #pragma message
 * （而且它只在 LVGL 的编译单元里可见，容易漏掉）。
 */
#ifndef LV_CONF_H
#define LV_CONF_H 1

/*=========================================================================
   颜色与像素
 *========================================================================*/
/* 16 位 RGB565：与显示 HAL、ST7789（16 位并口/SPI 目标）一致。
 * 宿主 SDL 纹理用 SDL_PIXELFORMAT_RGB565，二者逐像素等价，不做任何转换。 */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

/*=========================================================================
   内存
 *========================================================================*/
/* 1 = 不用 LVGL 自带的 lv_mem 池，全部走我们给的分配器：
 * 宿主侧由 host_lvgl_mem.cpp 包装 std::malloc/free 并统计（见 spec §10 的对象总量上限），
 * 目标侧（issue 11）再决定内部 SRAM / PSRAM 策略。 */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
    #define LV_MEM_CUSTOM_INCLUDE "embark_lvgl_hooks.h" /* C 侧声明，见 config/embark_lvgl_hooks.h */
    #define LV_MEM_CUSTOM_ALLOC embark_lvgl_alloc
    #define LV_MEM_CUSTOM_FREE embark_lvgl_free
    #define LV_MEM_CUSTOM_REALLOC embark_lvgl_realloc
#endif

/* 自带 lv_mem 池才用到的项，显式给出以免哪天切回 LV_MEM_CUSTOM=0 时套用上游默认。 */
#define LV_MEM_SIZE (48U * 1024U)
#define LV_MEM_BUF_MAX_NUM 16

/* 拷贝/置零走 libc：宿主是 MSVCRT/UCRT，目标是 newlib，两边都有优化过的实现。 */
#define LV_MEMCPY_MEMSET_STD 1

/*=========================================================================
   HAL 与调度
 *========================================================================*/
/* 我们都是自己在 UI 循环 / UI 任务里驱动：先 lv_tick_inc(delta)，再 lv_timer_handler()。 */
#define LV_TICK_CUSTOM 0
/* 刷新周期 30 → 20 ms（2026-10-17）：这是"状态变了到屏幕变"的最后一段延迟，按下确认之后
   还要等它；S3 上按脏区重绘，实测帧耗时均值 <1 ms、峰值也只有切屏那一帧几十 ms，50 Hz 的
   预算够用。注意真正的重绘耗时由 SPI 80 MHz 决定（整屏约 15 ms），别指望把它压到个位数。 */
#define LV_DISP_DEF_REFR_PERIOD 20
/* 触摸按 10 ms 采一次（2026-10-17 真机：30 ms 时快速轻划只被采到两三个点，LVGL 攒不够
   滚动阈值就把一次滑动当成点击；30→15 之后又跟着驱动侧一起收到 10 ms，跟手延迟里这一段
   只剩 10 ms）。驱动侧 poll 是 5 ms 一拍、事件排队不会丢，这里调小只是让 LVGL 更早把队列
   里的移动取走。空队列时这次读几乎没有开销。 */
#define LV_INDEV_DEF_READ_PERIOD 10

/* 240×320 的 2.8 寸屏约 143 dpi；取默认 130 让默认控件尺寸不至于过小。 */
#define LV_DPI_DEF 130

/*=========================================================================
   日志与断言
 *========================================================================*/
/* 打开 LVGL 日志但走回调（LV_LOG_PRINTF=0），由 platform/host/lvgl_port.cpp 注册
 * lv_log_register_print_cb 转发到 HAL 的 ILogSink；这样宿主与目标的日志出路一致。 */
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
#endif

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/* 断言不用 while(1)，改走框架的 fatal 通道（见 embark_lvgl_hooks.h）。
 * 末尾的分号是必须的：LVGL 在 lv_assert.h:37-43 把 LV_ASSERT_HANDLER 展开成一个
 * 语句（默认值就是 `while(1);`），少一个分号会编不过。 */
#define LV_ASSERT_HANDLER_INCLUDE "embark_lvgl_hooks.h"
#define LV_ASSERT_HANDLER embark_lvgl_assert_failed();

/*=========================================================================
   字体：默认 14 号 + EEZ 生成代码用到的 5 个字号（issue 19 / ADR 0008）
 *========================================================================*/
#define LV_FONT_MONTSERRAT_14 1

/* 默认字体 = 静态子集字库 embark_zh_14（2026-10-16 起）。
 * 之前它只被编译、没有任何代码引用它，静态库成员在链接期被丢弃 —— 字库其实
 * 没进固件镜像（build-esp32/embark_esp32.map 里搜不到该符号，固件大小也不随
 * 字库扩容变化）。让它当 LVGL 默认字体后：未显式设置字体的文本（框架壳、
 * bring-up 自检屏、宿主演示）有了 CJK 承载，缺字审计
 * （tools/font/audit_glyphs.mjs）的运行期承载者也随之成立。
 * 声明必须走 LV_FONT_CUSTOM_DECLARE：本文件被包含时 lv_font_t 还没定义，
 * 直接写 extern const lv_font_t 编不过（宏在 lv_font.h:244 展开）。 */
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(embark_zh_14)
#define LV_FONT_DEFAULT &embark_zh_14
/* EEZ Studio 导出页面（app/eez_ui/screens.c）引用的 Montserrat 字号：
 * 12/18/20/24/32/44/48。不开它们，EEZ 页面的文字全落到默认 14 号
 * （不是报错，只是字号不对）。12 与 44 是 2026-10-12 画 clock 屏时补的
 * （星期小字 + 46px 大时间）。加字号后构建期不会报错，只会静默变小 ——
 * 改屏后拿 screens.c 里的 lv_font_montserrat_N 对一遍本清单。
 *
 * 缺字审计（issue 17）相应扩展到 app/eez_ui/。注意 EEZ 页面**可以用 CJK**：
 * 工程自带字体资源 sim（ui_font_sim.c，源字体 simhei.ttf），字号 16、
 * 覆盖 0x20-0x7F + 0xB0 + 工程里 lvglSymbols 列出的全部汉字；clock 屏的
 * 中文文案走的就是它。加中文 = 往该字体资源的字符集里加字再重生成
 * （流程见 docs/guides/eez-studio-guide.md「字体文件 ui_font_sim.c」）。 */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_44 1
#define LV_FONT_MONTSERRAT_48 1
/* 其余 LV_FONT_* 由 lv_conf_internal.h 兜为 0（已核实，例如 :994-998 MONTSERRAT_8）。

 * 静态子集字库 embark_zh_14（tools/font/gen_font.mjs 生成，assets/fonts/embark_zh_14.c）
 * 是压缩位图（.bitmap_format = 1），必须开 LV_USE_FONT_COMPRESSED=1：
 * lv_font_fmt_txt.c:130-132 在未开启时对压缩字形只 LV_LOG_WARN 并返回 NULL 位图，
 * 结果就是"字全部不显示"。该字库原为导航壳使用（状态行/返回键），导航壳已于
 * 2026-10-06 退役；自 2026-10-16 起它顶上 LV_FONT_DEFAULT（见上方），不再只是
 * "保留备用" —— 真机与宿主上没显式设字体的文本都由它渲染。 */
#define LV_USE_FONT_COMPRESSED 1

/*=========================================================================
   主题、示例与诊断开关
 *========================================================================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#define LV_USE_PERF_MONITOR 1
#define LV_USE_MEM_MONITOR 1
#define LV_USE_SNAPSHOT 0

/* 应用侧要把 App 指针挂到控件上（issue 06/07），保留 user_data。 */
#define LV_USE_USER_DATA 1

/* 用 LVGL 自带 sprintf（自带实现，不依赖 newlib 的浮点格式化）。 */
#define LV_SPRINTF_CUSTOM 0

#endif /* LV_CONF_H */
