/**
 * EEZ UI 薄桥（issue 19 / ADR 0008）：把 EEZ 生成代码接到 Embark App 契约。
 *
 * 生成代码是 C（EEZ_FOR_LVGL 下 ui_init/ui_tick + Flow 运行时），不直接认识
 * Embark；这一层用四句话把两者接起来：
 *   - eez_ui_bridge_init()      onCreate 里调一次 = ui_init()（Flow 启动，建屏）；
 *   - eez_ui_bridge_tick()      onForegroundTick 每帧一次 = ui_tick()；
 *   - eez_ui_bridge_load_current_screen()  onEnter/onResume = 把 Flow 当前屏
 *     lv_scr_load 上来（Flow 的 eez_flow_set_screen 只建不载，载屏归 Embark）；
 *   - eez_ui_bridge_counter()   观测桥（验收用）= 生成代码 vars.cpp 的全局
 *     counter（+1/−1 按钮的落脚点）。
 *
 * 头只 include <lvgl.h>：不把 <eez/...> 泄进 Embark App 编译面。
 */
#ifndef EMBARK_APP_EEZ_UI_BRIDGE_H
#define EMBARK_APP_EEZ_UI_BRIDGE_H

#include <lvgl.h>

namespace embark::demo {

void eez_ui_bridge_init();
void eez_ui_bridge_tick();
void eez_ui_bridge_load_current_screen();
int eez_ui_bridge_counter();

}  // namespace embark::demo

#endif /* EMBARK_APP_EEZ_UI_BRIDGE_H */
