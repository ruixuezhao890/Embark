# 自带示例 App（spec §11 的 app/；issues/08 落地）
#
# 两个角色：
#   1. embark::apps（INTERFACE）—— 纯约定：App 层的编译规矩（build_options +
#      kernel_flags + include 根指向本目录），供任何编译"App 代码"的目标用。
#   2. embark::demo_apps（STATIC）—— 启动器 + clock + settings 共三个 App 的编译单元；
#      LauncherApp（issue 16 主屏）与 demo apps 共用同一份编译规则；画 UI 需要
#      LVGL，所以只在宿主 SDL2/LVGL 可用时构建（LVGL 由宿主分支带进来）。
#      App 之间有"换后端不动 app/ 与 core/include/embark/"的验收（spec §14.5）：
#      本目录只 include <embark/*>、<lvgl.h> 与 <middleware/etl/*>，
#      不碰任何 host/、device/ 后端头；真机（issues/11）按 core/cmake/embark_sources.cmake 的
#      EMBARK_APP_SOURCES_DEVICE 子集把各 App 的 .cpp 编进 IDF 组件（issue 20 起
#      两边共用同一份清单，不再手抄）。
#
#      目录布局（2026-10-06 目录化）：app/<app名>/ 放各 App 的 {h,cpp}；
#      app/common/ 放跨 App 共用代码：界面薄桥（aidogui_ui_bridge / aidogui_ui_nav /
#      ui_screen_names）与共享消息 app_messages.h。每个 App 独立成目录，
#      新 App：node tools/new_app.mjs <名> --apply —— 它读 app/music/ 生成薄壳，并顺手登记
#      cmake 的两份清单、ui_demo.cpp 的 include 与 EMBARK_APP_TABLE、aidogui_ui_hooks.h
#      （见 docs/guides/new-app-guide.md §2）。
#
# App 之间只通过框架消息通信，禁止互相 include（spec §4 / §7）。
add_library(embark_apps INTERFACE)
add_library(embark::apps ALIAS embark_apps)

target_link_libraries(embark_apps INTERFACE
        embark_build_options
        embark_kernel_flags)
target_include_directories(embark_apps INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")

# AiDoGUI Designer 生成代码（ADR 0011）：app/aidogui_ui/，编译口径见该目录 CMakeLists.txt。
add_subdirectory(aidogui_ui)

# 源清单来自 core/cmake/embark_sources.cmake（宿主与真机共用，issue 20）：这里以前是手写的一份，
# 真机组件另有一份，两边漂移的代价全落在真机链接期。加/删 App 改那份清单，
# 漏了会被它的配置期审计拦住。

embark_find_sdl2(EMBARK_APP_SDL2_FOUND)

if(EMBARK_APP_SDL2_FOUND)
  add_library(embark_demo_apps STATIC ${EMBARK_APP_SOURCES})
  add_library(embark::demo_apps ALIAS embark_demo_apps)

  # PUBLIC：各 App 头公开 include <embark/app.h> 与 <lvgl.h>，使用方需要
  # 内核与 LVGL 的 include 根（spec §11）。
  target_link_libraries(embark_demo_apps PUBLIC embark_apps embark::core embark_lvgl)

  # AiDoGUI 生成代码 + 资源盘（ADR 0011）：PRIVATE —— include 根不进消费者编译面
  # （生成代码头写死 <lvgl/lvgl.h>，与 App 自己的 include 面混在一起容易撞名）。
  target_link_libraries(embark_demo_apps PRIVATE embark::aidogui_ui embark::aidogui_res)
  # 生成代码头写死 <lvgl/lvgl.h>：需要 third_party/ 作为 include 根（spike 18 同款）。
  target_include_directories(embark_demo_apps SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party")

  # 界面后端开关（ADR 0011）：定义后 App 层的 ui_backend_* 桥调用生效（选后端的地方
  # 只有 app/common/ui_backend.h 一处）。esp32 真机在 components/embark 上定义同一个宏。
  target_compile_definitions(embark_demo_apps PRIVATE EMBARK_AIDOGUI_UI=1)
else()
  message(STATUS "  示例 App 库（embark_demo_apps）已跳过：没有 SDL2，LVGL 不可用。")
endif()