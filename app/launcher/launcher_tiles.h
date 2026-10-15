/**
 * Embark · 启动器磁贴构建（LVGL 侧，issue launcher-pages）
 *
 * 主屏的 App 磁贴不再画在 AiDoGUI 工程里：设计器里手摆的磁贴数 = 当时写死的 App 数，
 * 加一个 App 就得回去改设计稿，而且屏幕只有一页、后面的 App 根本够不着。
 * 现在屏上只留两个空容器：
 *   - Band_Apps：横向可滚的"磁贴带"（吸附式连续滚动，一页 = 一个视口宽）；
 *   - Dots_Row ：页码点容器（有几个 App 页就几个点）。
 * 本模块按 App 元数据（LauncherTile 数组，由 LauncherApp 从注册表取）在建好的屏上
 * 生成页容器、磁贴与页码点 —— 加 App 只改注册表，界面自己长出来。
 *
 * 分工：分页算术在 launcher_pages.h（纯函数，宿主单测直接算）；本模块只管 LVGL 对象、
 * 样式与事件。磁贴点击直接走 aidogui_ui_nav_request_app()：磁贴是运行时对象，塞不进
 * aidogui_ui_nav 那张 8 条的点击绑定表（那条路是给设计器里画死的控件留的备用路径）。
 *
 * 零堆纪律：页/磁贴/点都是 LVGL 对象，g_tiles 只是指向它们的定长数组。
 */
#ifndef EMBARK_APP_LAUNCHER_TILES_H
#define EMBARK_APP_LAUNCHER_TILES_H

#include <cstddef>

#include <lvgl.h>

namespace embark::demo {

/// 一个磁贴的元数据（字符串由调用方持有；来自 App 的 name()/title()，都是静态字面量）。
struct LauncherTile {
  const char* app_name = nullptr;  // 点它切到哪个 App
  const char* label = nullptr;     // 磁贴文案（通常是 App 的 title()）
};

/// 在 AiDoGUI 屏 <screen_name> 上重建磁贴/页/点；返回建出的磁贴数。
/// 屏名不是主屏、屏没装载、或设计器里少了 Band_Apps / Dots_Row 时返回 0 并记日志
/// （不抛、不崩：加 App 的人看不到主屏，比整个启动器起不来好）。
/// 可重复调用（屏重建后必须再调，控件是新的对象）。
std::size_t launcher_tiles_rebuild(const char* screen_name, const LauncherTile* tiles,
                                   std::size_t count);

// --- 验收观测点（ui_demo.cpp 的 --ui-launcher-pages 用）----------------------
/// 当前建出的磁贴数 / 页数 / 页码点数（屏未装载或未重建时都是 0）。
std::size_t launcher_tiles_count();
std::size_t launcher_tiles_page_count();
std::size_t launcher_tiles_dot_count();

/// 磁贴带当前停在第几页（0 起；按 scroll_x 就近取整）。
std::size_t launcher_tiles_current_page();

/// 磁贴带容器 / 第 index 个页容器 / 第 index 个磁贴 / 磁贴里的文案 label / 第 index 个点。
/// 越界或未建返回 nullptr。
lv_obj_t* launcher_tiles_band();
lv_obj_t* launcher_tiles_page(std::size_t index);
lv_obj_t* launcher_tiles_tile(std::size_t index);
lv_obj_t* launcher_tiles_tile_label(std::size_t index);
lv_obj_t* launcher_tiles_dot(std::size_t index);

/// 第 index 个磁贴绑定的 App 名（越界 nullptr）。
const char* launcher_tiles_app(std::size_t index);

}  // namespace embark::demo

#endif /* EMBARK_APP_LAUNCHER_TILES_H */
