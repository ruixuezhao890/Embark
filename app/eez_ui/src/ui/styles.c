#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: button
//

void init_style_button_MAIN_PRESSED(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0x881835));
};

lv_style_t *get_style_button_MAIN_PRESSED() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_button_MAIN_PRESSED(style);
    }
    return style;
};

void add_style_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_button_MAIN_PRESSED(), LV_PART_MAIN | LV_STATE_PRESSED);
};

void remove_style_button(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_button_MAIN_PRESSED(), LV_PART_MAIN | LV_STATE_PRESSED);
};

//
// Style: launcher_style
//

void init_style_launcher_style_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(0x175ccf));
    lv_style_set_bg_opa(style, 255);
};

lv_style_t *get_style_launcher_style_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_launcher_style_MAIN_DEFAULT(style);
    }
    return style;
};

void init_style_launcher_style_SCROLLBAR_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_opa(style, 0);
};

lv_style_t *get_style_launcher_style_SCROLLBAR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_launcher_style_SCROLLBAR_DEFAULT(style);
    }
    return style;
};

void add_style_launcher_style(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_launcher_style_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_launcher_style_SCROLLBAR_DEFAULT(), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
};

void remove_style_launcher_style(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_launcher_style_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_launcher_style_SCROLLBAR_DEFAULT(), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_button,
        add_style_launcher_style,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_button,
        remove_style_launcher_style,
    };
    remove_style_funcs[styleIndex](obj);
}