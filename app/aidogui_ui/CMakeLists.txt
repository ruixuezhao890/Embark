# AiDoGUI 生成代码（ADR 0011：界面代码的唯一来源）。
#
# export/* —— AiDoGUI 自己的导出布局（GUI 导出向导与 tools/aidogui/export.ps1 写的是同一份）：
#             ui.c/ui.h/ui_helpers.*/ui_comm.*/ui_events.h/filelist.txt + screens/ + components/
#             + font/*.bin（有图片时还会生成 image/）。导出产物勿手改，重新生成见 README.md。
#
# 屏表/控件表/字体表不在这里手写：cmake/embark_aidogui_tables.cmake 在配置期解析导出产物，
# 生成 ${CMAKE_CURRENT_BINARY_DIR}/include/embark_aidogui_ui_tables.h（不手改、不入库）。
#
# 生成代码是第三方产物：压掉告警（-w），不让它进本仓库的告警门禁。
file(GLOB AIDOGUI_UI_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/export/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/screens/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/export/components/*.c")
if(NOT AIDOGUI_UI_SOURCES)
  message(WARNING "app/aidogui_ui/export 下没有 C 代码：先在 AiDoGUI 里导出一次（或跑 tools/aidogui/export.ps1）。")
endif()

add_library(embark_aidogui_ui STATIC ${AIDOGUI_UI_SOURCES})
add_library(embark::aidogui_ui ALIAS embark_aidogui_ui)

# 配置期从导出产物建表（跨平台，不依赖 PowerShell，也不需要装设计器）。
include(embark_aidogui_tables)
embark_generate_aidogui_tables("${CMAKE_CURRENT_SOURCE_DIR}/export"
                               "${CMAKE_CURRENT_BINARY_DIR}/include/embark_aidogui_ui_tables.h")

# 唯一暴露给消费者的东西：构建期生成的表头（自含 —— 自己 extern 用到的符号 + <lvgl.h>，
# 不 include 生成代码的 ui.h —— 那份头是给屏内 C 代码互相包含用的，不该进 App 编译面）。
target_include_directories(embark_aidogui_ui PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
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
