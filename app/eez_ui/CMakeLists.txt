# EEZ Studio 生成代码（issue 19 / ADR 0008）
#
# 这 16 个文件是 EEZ Studio 导出的"生成代码"，按生成器口径编译，不套 Embark 的
# App 层规则：
#   - 生成器输出统一 -w（见下方 compile_options）——重新导出即整体覆盖，
#     逐条消警告没有意义；零异常/RTTI 红线仍生效（spec §10）。
#
#   - EEZ_FOR_LVGL / EEZ_PLATFORM_SIMULATOR 由依赖的 embark_eez 以 PUBLIC 携带；
#     生成代码写死 <lvgl/lvgl.h> 与 <eez/...>，故 SYSTEM 加 third_party/ 根，
#     以及本仓库生成的 C++ middleware 视图（仅 C++ 侧，见 repo CMakeLists 的
#     middleware_views）。
#
# 变量桥、事件回调桥接到 Embark App 的薄层在 app/eez_ui_bridge.{h,cpp}。

# 与样例一致的全部生成文件（16 个；fonts.h 只声明不定义，无对应 .c）。
set(EEZ_UI_SOURCES
        actions.cpp
        images.c
        screens.c
        styles.c
        ui.c
        ui_image_log_eez.c
        ui_image_logo_lvgl.c
        vars.cpp)

add_library(embark_eez_ui STATIC ${EEZ_UI_SOURCES})
add_library(embark::eez_ui ALIAS embark_eez_ui)

# C 标准：仓库在 cmake/embark_freertos.cmake 用 C_STANDARD 属性，但那里紧挨
# enable_language(C) 的编译器探测；本目录离探测点远，CMake 4.0 会在生成期
# 报 CMAKE_C_COMPILE_OBJECT 缺失。生成代码的 .c 文件用不着 C11 特性
# （EEZ 模板是 C89/99 风格），不设标准、交给编译器默认值即可（lvgl 同款）。

# 头文件是生成器自带全形，不重写其结构。
target_include_directories(embark_eez_ui PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party")
target_include_directories(embark_eez_ui SYSTEM PRIVATE "${CMAKE_BINARY_DIR}/include")

target_link_libraries(embark_eez_ui PUBLIC embark_eez)

# 生成器输出统一 -w（与 lvgl/eez-framework 同款纪律）：重新导出即整体覆盖，逐条消警告
# 没有意义。零异常/RTTI 红线对生成代码同样生效（spec §10）。
target_compile_options(embark_eez_ui PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-w>
        $<$<COMPILE_LANGUAGE:CXX>:-w;-fno-exceptions;-fno-rtti>)
