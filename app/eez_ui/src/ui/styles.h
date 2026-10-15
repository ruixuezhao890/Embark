#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: button
lv_style_t *get_style_button_MAIN_PRESSED();
void add_style_button(lv_obj_t *obj);
void remove_style_button(lv_obj_t *obj);

// Style: launcher_style
lv_style_t *get_style_launcher_style_MAIN_DEFAULT();
lv_style_t *get_style_launcher_style_SCROLLBAR_DEFAULT();
void add_style_launcher_style(lv_obj_t *obj);
void remove_style_launcher_style(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/