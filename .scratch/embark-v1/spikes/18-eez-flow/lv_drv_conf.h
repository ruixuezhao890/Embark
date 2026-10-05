/**
 * @file lv_drv_conf.h
 * @brief Spike 18 专用：只开 SDL 驱动（lv_drivers@8cdabe8d）。
 *
 * 与样例同款配置的裁剪版：USE_MONITOR/USE_KEYBOARD/USE_MOUSE/USE_MOUSEWHEEL
 * 一律不定义（=0），避免 sdl.c 的 deprecated 警告；其余显示/输入驱动全部关闭。
 */
#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

#define LV_DRV_DELAY_INCLUDE      <stdint.h>
#define LV_DRV_DELAY_US(us)       /*delay_us(us)*/
#define LV_DRV_DELAY_MS(ms)       /*delay_ms(ms)*/

#define LV_DRV_DISP_INCLUDE           <stdint.h>
#define LV_DRV_DISP_CMD_DATA(val)    /*pin_x_set(val)*/
#define LV_DRV_DISP_RST(val)         /*pin_x_set(val)*/
#define LV_DRV_DISP_SPI_CS(val)          /*spi_cs_set(val)*/
#define LV_DRV_DISP_SPI_WR_BYTE(data)    /*spi_wr(data)*/
#define LV_DRV_DISP_SPI_WR_ARRAY(adr, n) /*spi_wr_mem(adr, n)*/
#define LV_DRV_DISP_PAR_CS(val)          /*par_cs_set(val)*/
#define LV_DRV_DISP_PAR_SLOW             /*par_slow()*/
#define LV_DRV_DISP_PAR_FAST             /*par_fast()*/
#define LV_DRV_DISP_PAR_WR_WORD(data)    /*par_wr(data)*/
#define LV_DRV_DISP_PAR_WR_ARRAY(adr, n) /*par_wr_mem(adr,n)*/

#define LV_DRV_INDEV_INCLUDE     <stdint.h>
#define LV_DRV_INDEV_RST(val)    /*pin_x_set(val)*/
#define LV_DRV_INDEV_IRQ_READ    0
#define LV_DRV_INDEV_SPI_CS(val)            /*spi_cs_set(val)*/
#define LV_DRV_INDEV_SPI_XCHG_BYTE(data)    0
#define LV_DRV_INDEV_I2C_START              /*i2c_start()*/
#define LV_DRV_INDEV_I2C_STOP               /*i2c_stop()*/
#define LV_DRV_INDEV_I2C_RESTART            /*i2c_restart()*/
#define LV_DRV_INDEV_I2C_WR(data)           /*i2c_wr(data)*/
#define LV_DRV_INDEV_I2C_READ(last_read)    0

extern int monitor_hor_res, monitor_ver_res;

#define USE_SDL         1
#if USE_SDL
#define SDL_HOR_RES         monitor_hor_res
#define SDL_VER_RES         monitor_ver_res
#define SDL_ZOOM            1
#define SDL_INCLUDE_PATH    <SDL2/SDL.h>
#define SDL_VIRTUAL_MACHINE 1
#endif

#endif /* LV_DRV_CONF_H */
