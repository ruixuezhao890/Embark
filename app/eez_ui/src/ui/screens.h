#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_LAUNCHER = 1,
    SCREEN_ID_CLOCK = 2,
    _SCREEN_ID_LAST = 2
};

typedef struct _objects_t {
    lv_obj_t *launcher;
    lv_obj_t *clock;
    lv_obj_t *launcher_side;
    lv_obj_t *hour_label;
    lv_obj_t *min_label;
    lv_obj_t *date_label;
    lv_obj_t *launcher_right;
    lv_obj_t *weather_card;
    lv_obj_t *weather_info;
    lv_obj_t *weather_icon;
    lv_obj_t *weather_temp;
    lv_obj_t *weather_city;
    lv_obj_t *weather_loc;
    lv_obj_t *weather_desc;
    lv_obj_t *weather_hl;
    lv_obj_t *apps_label;
    lv_obj_t *apps_list;
    lv_obj_t *app_row_settings;
    lv_obj_t *app_settings_icon;
    lv_obj_t *app_settings_name;
    lv_obj_t *app_settings_arr;
    lv_obj_t *app_row_music;
    lv_obj_t *app_music_icon;
    lv_obj_t *app_music_name;
    lv_obj_t *app_music_arr;
    lv_obj_t *app_row_health;
    lv_obj_t *app_health_icon;
    lv_obj_t *app_health_name;
    lv_obj_t *app_health_arr;
    lv_obj_t *app_row_photo;
    lv_obj_t *app_photo_icon;
    lv_obj_t *app_photo_name;
    lv_obj_t *app_photo_arr;
    lv_obj_t *app_row_map;
    lv_obj_t *app_map_icon;
    lv_obj_t *app_map_name;
    lv_obj_t *app_map_arr;
    lv_obj_t *app_row_calc;
    lv_obj_t *app_calc_icon;
    lv_obj_t *app_calc_name;
    lv_obj_t *app_calc_arr;
    lv_obj_t *app_row_note;
    lv_obj_t *app_note_icon;
    lv_obj_t *app_note_name;
    lv_obj_t *app_note_arr;
    lv_obj_t *app_row_alarm;
    lv_obj_t *app_row_camera;
    lv_obj_t *app_camera_icon;
    lv_obj_t *app_camera_name;
    lv_obj_t *app_camera_arr;
    lv_obj_t *app_row_mail;
    lv_obj_t *app_mail_icon;
    lv_obj_t *app_mail_name;
    lv_obj_t *app_mail_arr;
    lv_obj_t *clock_feed;
    lv_obj_t *card_time;
    lv_obj_t *lbl_time;
    lv_obj_t *lbl_sw;
    lv_obj_t *lbl_date;
    lv_obj_t *card_cal;
    lv_obj_t *lbl_cal_k;
    lv_obj_t *lbl_cal_r;
    lv_obj_t *lbl_cal;
    lv_obj_t *card_today;
    lv_obj_t *lbl_today_k;
    lv_obj_t *lbl_doy_c;
    lv_obj_t *lbl_doy;
    lv_obj_t *lbl_doy_t;
    lv_obj_t *lbl_p1k;
    lv_obj_t *lbl_p1;
    lv_obj_t *bar_prog;
    lv_obj_t *lbl_p2k;
    lv_obj_t *lbl_p2;
    lv_obj_t *card_tmrw;
    lv_obj_t *lbl_tmrw_k;
    lv_obj_t *lbl_tmrw_r;
    lv_obj_t *lbl_tm_k;
    lv_obj_t *lbl_tm;
    lv_obj_t *lbl_tw_k;
    lv_obj_t *lbl_tw;
    lv_obj_t *card_month;
    lv_obj_t *lbl_month_k;
    lv_obj_t *lbl_dim_c;
    lv_obj_t *lbl_dim;
    lv_obj_t *lbl_dim_t;
    lv_obj_t *lbl_fd_k;
    lv_obj_t *lbl_fd;
    lv_obj_t *lbl_fw_k;
    lv_obj_t *lbl_fw;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
} objects_t;

extern objects_t objects;

void create_screen_launcher();
void delete_screen_launcher();
void tick_screen_launcher();

void create_screen_clock();
void delete_screen_clock();
void tick_screen_clock();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/