/**
 * @file main.c
 * @brief Spike 18 主程序：原生 SDL 循环（替换样例的 emscripten 版）。
 *
 * 流程：lv_init → sdl 显示/输入接线 → ui_init()（EEZ Flow 运行时 + 生成 UI）
 * → 跑 N 帧 lv_timer_handler + ui_tick → 退出并打印三处堆观测。
 * 用法：spike18 [帧数] [宽] [高]（默认 600 帧 800x480，与样例 UI 布局一致）。
 */
#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "lvgl/lvgl.h"
#include "sdl/sdl.h"

#include "ui/ui.h"
#include "embark_lvgl_hooks.h"

int monitor_hor_res = 800;
int monitor_ver_res = 480;

static void hal_init(void) {
    sdl_init();

    static lv_disp_draw_buf_t disp_buf;
    lv_color_t *buf1 = (lv_color_t *)malloc(sizeof(lv_color_t) * monitor_hor_res * monitor_ver_res);
    if (!buf1) { printf("[spike18] draw buffer alloc failed\n"); exit(2); }
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, monitor_hor_res * monitor_ver_res);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = monitor_hor_res;
    disp_drv.ver_res = monitor_ver_res;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_mouse, indev_kb, indev_enc;
    lv_indev_drv_init(&indev_mouse);
    indev_mouse.type = LV_INDEV_TYPE_POINTER;
    indev_mouse.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_mouse);

    lv_indev_drv_init(&indev_kb);
    indev_kb.type = LV_INDEV_TYPE_KEYPAD;
    indev_kb.read_cb = sdl_keyboard_read;
    lv_indev_drv_register(&indev_kb);

    lv_indev_drv_init(&indev_enc);
    indev_enc.type = LV_INDEV_TYPE_ENCODER;
    indev_enc.read_cb = sdl_mousewheel_read;
    lv_indev_drv_register(&indev_enc);
}

static void print_metrics(const char *tag) {
    size_t out = embark_lvgl_outstanding_bytes();
    size_t peak = embark_lvgl_peak_bytes();
    size_t budget = embark_lvgl_budget_bytes();
    printf("[spike18] %-20s outstanding=%zu peak=%zu  peak/budget=%.1f%% (budget=%zu)\n",
           tag, out, peak, 100.0 * (double)peak / (double)budget, budget);
}

int main(int argc, char **argv) {
    int frames = (argc > 1) ? atoi(argv[1]) : 600;
    if (argc > 2) monitor_hor_res = atoi(argv[2]);
    if (argc > 3) monitor_ver_res = atoi(argv[3]);
    printf("[spike18] %dx%d frames=%d\n", monitor_hor_res, monitor_ver_res, frames);

    lv_init();
    hal_init();

    print_metrics("before ui_init");
    ui_init();
    print_metrics("after ui_init");

    for (int i = 0; i < frames; i++) {
        lv_tick_inc(5);
        lv_timer_handler();
        ui_tick();
        SDL_Delay(5);
    }
    print_metrics("after loop");
    return 0;
}