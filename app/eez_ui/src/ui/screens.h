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
    lv_obj_t *label;
    lv_obj_t *obj0;
    lv_obj_t *btn;
    lv_obj_t *obj1;
} objects_t;

extern objects_t objects;

void create_screen_launcher();
void tick_screen_launcher();

void create_screen_clock();
void tick_screen_clock();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/