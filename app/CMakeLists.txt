# 自带示例 App（spec §11 的 app/；issues/08 落地）
#
# 两个角色：
#   1. embark::apps（INTERFACE）—— 纯约定：App 层的编译规矩（build_options +
#      kernel_flags + include 根指向本目录），供任何编译"App 代码"的目标用。
#   2. embark::demo_apps（STATIC）—— 启动器 + clock + settings 共三个 App 的编译单元；
#      LauncherApp（issue 16 主屏）与 demo apps 共用同一份编译规则；画 UI 需要
#      LVGL，所以只在宿主 SDL2/LVGL 可用时构建（LVGL 由宿主分支带进来）。
#      App 之间有"换后端不动 app/ 与 include/embark/"的验收（spec §14.5）：
#      本目录只 include <embark/*>、<lvgl.h> 与 <middleware/etl/*>，
#      不碰任何 platform/ 后端头；真机（issues/11）按下面的目录清单把
#      各 App 的 .cpp 编进 IDF 组件即可（app/CMakeLists.txt 源列表即真机清单）。
#
#      目录布局（2026-10-06 目录化）：app/<app名>/ 放各 App 的 {h,cpp}；
#      app/common/ 放跨 App 共用代码：EEZ 薄桥（eez_ui_bridge / eez_ui_nav /
#      eez_ui_screen_names）与共享消息 app_messages.h。每个 App 独立成目录，
#      新 App 用 tools/scaffold_app.py 一键生成（自动登记到本文件的源列表与
#      EMBARK_APP_TABLE，见 docs/quickstart.md）。
#
# App 之间只通过框架消息通信，禁止互相 include（spec §4 / §7）。
add_library(embark_apps INTERFACE)
add_library(embark::apps ALIAS embark_apps)

target_link_libraries(embark_apps INTERFACE
        embark_build_options
        embark_kernel_flags)
target_include_directories(embark_apps INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")

# EEZ Studio 生成代码（issue 19 / ADR 0008）：app/eez_ui/，编译口径见该目录 CMakeLists.txt。
add_subdirectory(eez_ui)

# 真机（esp32，issues/11）组件清单与此源列表一一对应（components/embark/CMakeLists.txt）。

embark_find_sdl2(EMBARK_APP_SDL2_FOUND)

if(EMBARK_APP_SDL2_FOUND)
  # 三个 demo App 各占一个目录（2026-10-06 目录化）：app/launcher/、app/clock/、app/settings/；
  # 公共件在 app/common/（薄桥 + app_messages.h）。新 App 用 tools/scaffold_app.py
  # 生成：它把 <name>_app.cpp 追加进下面的源列表（锚点 # 真机清单锚点）。
  # 真机清单锚点（scaffold_app.py 在这里追加新 App 的源文件）：
  add_library(embark_demo_apps STATIC
          launcher/launcher_app.cpp
          clock/clock_app.cpp
          settings/settings_app.cpp
          common/eez_ui_bridge.cpp
          common/eez_ui_nav.cpp)
  add_library(embark::demo_apps ALIAS embark_demo_apps)

  # PUBLIC：各 App 头公开 include <embark/app.h> 与 <lvgl.h>，使用方需要
  # 内核与 LVGL 的 include 根（spec §11）。
  target_link_libraries(embark_demo_apps PUBLIC embark_apps embark::core embark_lvgl)

  # EEZ 生成代码与薄桥（issue 19）：PRIVATE —— 静态库依赖以 LINK_ONLY 透传给
  # 宿主可执行文件，但 eez 的 include 根不进消费者的编译面。
  # 只链 embark_eez_ui：生成 eez-flow.cpp 是自含混编（commit 10f714f），不再链
  # third_party 的 eez_framework（另一个 pin，同链必重复符号，见 eez_ui/CMakeLists.txt）。
  target_link_libraries(embark_demo_apps PRIVATE embark::eez_ui)
  # 生成代码头写死 <lvgl/lvgl.h>：需要 third_party/ 作为 include 根（spike 18 同款）。
  target_include_directories(embark_demo_apps SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party")

  # 薄壳开关（issue 19）：宿主构建接 EEZ 生成代码 —— App 层的 eez_ui_bridge_*
  # 桥调用生效；esp32 真机组件清单没编 eez_ui_*/生成代码，不定义此宏 = 桥调用
  # 编译成 no-op（见 launcher_app.cpp / clock_app.cpp / settings_app.cpp 的
  # #if defined(EMBARK_EEZ_UI_BRIDGE)）。
  target_compile_definitions(embark_demo_apps PRIVATE EMBARK_EEZ_UI_BRIDGE=1)
else()
  message(STATUS "  示例 App 库（embark_demo_apps）已跳过：没有 SDL2，LVGL 不可用。")
endif()