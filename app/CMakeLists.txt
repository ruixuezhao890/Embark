# 自带示例 App（spec §11 的 app/；issues/08 落地）
#
# 两个角色：
#   1. embark::apps（INTERFACE）—— 纯约定：App 层的编译规矩（build_options +
#      kernel_flags + include 根指向本目录），供任何编译"App 代码"的目标用。
#   2. embark::demo_apps（STATIC）—— 三个 demo App 的编译单元；画 UI 需要 LVGL，
#      所以只在宿主 SDL2/LVGL 可用时构建（LVGL 由宿主分支带进来）。
#      App 之间有"换后端不动 app/ 与 include/embark/"的验收（spec §14.5）：
#      本目录只 include <embark/*>、<lvgl.h> 与 <middleware/etl/*>，
#      不碰任何 platform/ 后端头；真机（issues/11）把 demo_apps.cpp 编进
#      IDF 组件即可，本文件不用改。
#
# App 之间只通过框架消息通信，禁止互相 include（spec §4 / §7）。
add_library(embark_apps INTERFACE)
add_library(embark::apps ALIAS embark_apps)

target_link_libraries(embark_apps INTERFACE
        embark_build_options
        embark_kernel_flags)
target_include_directories(embark_apps INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

embark_find_sdl2(EMBARK_APP_SDL2_FOUND)

if(EMBARK_APP_SDL2_FOUND)
  add_library(embark_demo_apps STATIC demo_apps.cpp hello_app.cpp)
  add_library(embark::demo_apps ALIAS embark_demo_apps)

  # PUBLIC：demo_apps.h 公开 include <embark/app.h> 与 <lvgl.h>，使用方需要
  # 内核与 LVGL 的 include 根（spec §11）。
  target_link_libraries(embark_demo_apps PUBLIC embark_apps embark::core embark_lvgl)
else()
  message(STATUS "  示例 App 库（embark_demo_apps）已跳过：没有 SDL2，LVGL 不可用。")
endif()