# EEZ Studio 生成代码（issue 19 / ADR 0008）：app/eez_ui/src/ui/
#
# 纪律（生成代码永不手改，本文件是唯一适配点）：
#   1. 文件集不固定：EEZ Studio 每次导出都会增删文件，这里用 GLOB +
#      CONFIGURE_DEPENDS 自动收编 src/ui/ 下的 .c/.cpp（顶层 cmake_minimum 3.24，
#      CONFIGURE_DEPENDS 可用）；未来新增文件无需改本文件，重新配置即可。CMake
#      首次配置时目录必须非空（GLOB 空集是配置错误），src/ui/ 随仓库存在，无虞。
#   2. 编译口径按第三方对待：生成代码来自 EEZ Studio（上游 eez-framework 混编），
#      不套 -Wall/-Wextra/-Wpedantic，统一 -w 压掉生成器输出的全部告警；也不套
#      embark_kernel_flags 的 -fno-exceptions/-fno-rtti（eez-flow.cpp 是上游全量
#      混编，RTTI/异常使用不受我们控制）。
#   3. 只链 LVGL，不链 embark_eez/eez_framework：生成的 eez-flow.cpp 是自含混编
#      （commit 10f714f，见其头部注释），已内嵌全部 flow 运行时；submodule 里的
#      eez-framework 是另一个 pin，同链必重复符号。
#   4. 生成代码头写死 <lvgl/lvgl.h>：需要 third_party/ 作为 include 根。
#   5. C 文件（screens.c/styles.c/ui.c/images.c）按 C 编：eez-flow.h 的 C 可解析面
#      （native_vars、flow/lvgl_api 声明）自带 extern "C" 守卫，混合编译安全。
#   6. structs.h 是纯 C++ 且仓库无引用，不编（GLOB *.c/*.cpp 天然排除）。
#
# 产物：embark_eez_ui（STATIC）。链接方 = embark_demo_apps（app/CMakeLists.txt）。
file(GLOB EEZ_UI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")

add_library(embark_eez_ui STATIC ${EEZ_UI_SOURCES})
add_library(embark::eez_ui ALIAS embark_eez_ui)

target_include_directories(embark_eez_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
# 生成代码头写死 <lvgl/lvgl.h>：third_party/ 作 include 根；eez-flow.h 自身的
# #if 分支会按 LVGL 版本决定是否再 include <lvgl/src/lvgl_private.h>（8.3 不引用）。
target_include_directories(embark_eez_ui SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party")

target_link_libraries(embark_eez_ui PUBLIC embark_lvgl)

set_target_properties(embark_eez_ui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
# 生成器输出统一 -w：告警不参与反馈，手改生成文件没有意义。
target_compile_options(embark_eez_ui PRIVATE -w)
# --- 屏表（屏名 → App 名）--------------------------------------------------
# 约定：EEZ 里 screen 名 == App 名，子页 = <app名>_<编号>_sub。生成代码每次导出
# 都可增删屏，所以这里解析不到屏就写空表（绝不 FATAL_ERROR）；除 GLOB 的
# CONFIGURE_DEPENDS 之外，再把 screens.h/screens.c 挂成 reconfigure 依赖，
# 导出后重新构建即自动刷新屏表，两个方向都不用回来改本文件。
include(embark_eez_screens)

set(EEZ_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
set(EEZ_SCREENS_HEADER "${CMAKE_BINARY_DIR}/include/embark_eez_screens.h")
foreach(_dep screens.h screens.c)
  if(EXISTS "${EEZ_UI_DIR}/${_dep}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EEZ_UI_DIR}/${_dep}")
  endif()
endforeach()
embark_generate_eez_screen_table("${EEZ_UI_DIR}" "${EEZ_SCREENS_HEADER}")
# --- 变量表（变量名 → Flow 全局变量索引）------------------------------------
# 变量是 UI 显示数据的接口（ADR 0008）：在 Studio 的 Flow 变量面板声明，导出后
# 出现在 vars.h 的 FlowGlobalVariables 枚举里。与屏表同样的纪律：解析不到就写空表
# （绝不 FATAL_ERROR），并把 vars.h 挂成 reconfigure 依赖 —— Studio 里增删变量后
# 重新构建即自动刷新变量表，C++ 侧一行都不用改。
include(embark_eez_vars)

set(EEZ_VARS_HEADER "${CMAKE_BINARY_DIR}/include/embark_eez_vars.h")
if(EXISTS "${EEZ_UI_DIR}/vars.h")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EEZ_UI_DIR}/vars.h")
endif()
embark_generate_eez_var_table("${EEZ_UI_DIR}/vars.h" "${EEZ_VARS_HEADER}")

