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
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

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
   字体：只留默认那一个
 *========================================================================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
/* 其余 LV_FONT_* 由 lv_conf_internal.h 兜为 0（已核实，例如 :994-998 MONTSERRAT_8）。

 * 静态子集字库 embark_zh_14（tools/font/gen_font.mjs 生成，assets/fonts/embark_zh_14.c）
 * 是压缩位图（.bitmap_format = 1），必须开 LV_USE_FONT_COMPRESSED=1：
 * lv_font_fmt_txt.c:130-132 在未开启时对压缩字形只 LV_LOG_WARN 并返回 NULL 位图，
 * 结果就是"字全部不显示"。启动器（LauncherApp）与导航壳（NavShell）依赖它。 */
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
