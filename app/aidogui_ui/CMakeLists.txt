# AiDoGUI 生成代码（ADR 0011：界面代码的唯一来源）。
#
# export/* —— AiDoGUI 自己的导出布局（GUI 导出向导与 tools/aidogui/export.ps1 写的是同一份）。
#             导出格式 v4（export/.aidogui-export.json 的 version）：ui.c/ui.h/ui_helpers.*/ui_comm.*
#             + screens/ + components/ + events/ + fonts/ + custom/ + animations/（有图片时还有
#             images/），以及描述"每屏有哪些控件"的 aidogui_objects.h。导出产物勿手改，
#             重新生成见 README.md。
#
# 屏表/控件表/字体表不在这里手写：core/cmake/embark_aidogui_tables.cmake 在配置期解析导出产物，
# 生成 ${CMAKE_CURRENT_BINARY_DIR}/include/embark_aidogui_ui_tables.h（不手改、不入库）。
#
# 生成代码是第三方产物：压掉告警（-w），不让它进本仓库的告警门禁。
file(GLOB AIDOGUI_UI_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/export/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/screens/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/components/*.c"
     # 导出格式 v4 新增的四类：字号槽、交互事件、工程自定义代码、动画。
     "${CMAKE_CURRENT_SOURCE_DIR}/export/fonts/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/events/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/custom/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/animations/*.c")
if(NOT AIDOGUI_UI_SOURCES)
  message(WARNING "app/aidogui_ui/export 下没有 C 代码：先在 AiDoGUI 里导出一次（或跑 tools/aidogui/export.ps1）。")
endif()

add_library(embark_aidogui_ui STATIC ${AIDOGUI_UI_SOURCES})
add_library(embark::aidogui_ui ALIAS embark_aidogui_ui)

# 配置期从导出产物建表（跨平台，不依赖 PowerShell，也不需要装设计器）。
include(embark_aidogui_tables)
embark_generate_aidogui_tables("${CMAKE_CURRENT_SOURCE_DIR}/export"
                               "${CMAKE_CURRENT_BINARY_DIR}/include/embark_aidogui_ui_tables.h")

# 暴露给消费者的两样：构建期生成的表头，和导出根（只为 <aidogui_objects.h> —— 导出格式 v4 把
# "屏根 + 每屏控件" 收进 aidogui_ui 这个 extern 结构体，表头要它的类型定义才能取到槽的地址）。
# 生成代码的 ui.h 仍然不该进 App 编译面：谁要 include 它，是自己越界，不是这里在邀请。
target_include_directories(embark_aidogui_ui PUBLIC
                           "${CMAKE_CURRENT_BINARY_DIR}/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}/export")
# 生成代码自己按相对路径 include 彼此（screens/x.c → "../ui.h"），不需要额外的 include 根；
# 只借 third_party 找 <lvgl/lvgl.h>。
target_include_directories(embark_aidogui_ui SYSTEM PRIVATE
                           "${PROJECT_SOURCE_DIR}/third_party")
target_link_libraries(embark_aidogui_ui PUBLIC embark_lvgl)
target_compile_options(embark_aidogui_ui PRIVATE -w)

# 设计器的「调用函数」事件生成一句裸调用（embark_ui_open_clock();），生成的 C 文件不认识
# 我们的头 —— 用编译器的强制注入（GCC/Clang 的 -include）把 hook 头塞进每个生成 C 文件，
# 于是事件编得过、生成文件仍然只读（ADR 0011 决策⑦）。宿主与 esp32 都用 GCC/Clang。
target_compile_options(embark_aidogui_ui PRIVATE
                       -include "${PROJECT_SOURCE_DIR}/app/common/aidogui_ui_hooks.h")