# Embark 内核组件（真机侧，issues/11）
#
# 这个组件**不复制任何源码**：SRCS / INCLUDE_DIRS 全用绝对路径指回仓库根，与宿主构建
# 共用同一份 src/、app/、platform/common/、platform/esp32/src/（spec §11 的"一份源码树，
# 两条构建路径"）。组件目录在 <repo>/platform/esp32/project/components/embark，
# 所以仓库根要上跳 5 级。

get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# --- 与宿主 src/CMakeLists.txt 的源列表保持一致（内核 4 个 TU） ----------------
# issue 13 起没有 src/embark/error.cpp：错误码文本改由 <embark/error.h> 的
# error_text() 内联提供，不再单独占一个 TU。
set(EMBARK_KERNEL_SOURCES
        "${EMBARK_ROOT}/src/embark/framework.cpp"
        "${EMBARK_ROOT}/src/embark/bus.cpp"
        "${EMBARK_ROOT}/src/embark/log.cpp"
        "${EMBARK_ROOT}/src/embark/version.cpp")

# --- Demo App（与宿主同一批 App：launcher/clock/settings 三个；目录化后各占一目录，
#     真机清单与 app/CMakeLists.txt 的源列表一一对应 —— §14.5 换后端、App 一行不改）---
# 桥调用在真机上 no-op（EMBARK_EEZ_UI_BRIDGE 未定义），真机接入 EEZ 后在此补编
# common/eez_ui_bridge.cpp 与 common/eez_ui_nav.cpp 并打开开关即可。
set(EMBARK_APP_SOURCES
        "${EMBARK_ROOT}/app/launcher/launcher_app.cpp"
        "${EMBARK_ROOT}/app/clock/clock_app.cpp"
        "${EMBARK_ROOT}/app/settings/settings_app.cpp")

# --- 宿主与真机共用的胶水：必须与 platform/common/CMakeLists.txt 的列表一致 ---
set(EMBARK_COMMON_SOURCES
        "${EMBARK_ROOT}/platform/common/static_pool.cpp"
        "${EMBARK_ROOT}/platform/common/lvgl_port.cpp"
        "${EMBARK_ROOT}/platform/common/lvgl_ui_port.cpp")

# --- ESP32 后端（platform/esp32/src/）------------------------------------------
set(EMBARK_ESP32_SOURCES
        "${EMBARK_ROOT}/platform/esp32/src/esp32_time.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_log_sink.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_system.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_persistence.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_bus.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_display.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_input.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_ui_task.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_lvgl_mem.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_hal.cpp")

idf_component_register(
        SRCS ${EMBARK_KERNEL_SOURCES}
             ${EMBARK_APP_SOURCES}
             ${EMBARK_COMMON_SOURCES}
             ${EMBARK_ESP32_SOURCES}
        INCLUDE_DIRS
             "${EMBARK_ROOT}/include"              # embark/*.h
             "${EMBARK_ROOT}/config"               # embark_config.h / embark_limits.h / lv_conf.h
             "${EMBARK_ROOT}/app"                  # <app名>/ 各 App 的 {h,cpp}
             "${EMBARK_ROOT}/app/common"           # app_messages.h（薄桥待接入 EEZ）
             "${EMBARK_ROOT}/platform/common"      # lvgl_port.h / lvgl_ui_port.h / static_pool.h
             "${EMBARK_ROOT}/platform/esp32/src"   # esp32_*.h
             "${CMAKE_BINARY_DIR}/include"         # middleware 视图（<middleware/etl|efmt|elog/...>）
             "${EMBARK_ROOT}/third_party/etl/include"  # <etl/...>（ETL 是 header-only）
        REQUIRES lvgl embark_font nvs_flash esp_timer esp_lcd driver)

# 内核与 App 的编译口径（与宿主 embark_kernel_flags 对齐：C++17、无异常、无 RTTI）。
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_EXTENSIONS OFF)

# config/embark_config.h 强制包含（宿主侧是 -include .../config/embark_config.h）：
# 它是 ETL 配置、日志开关、平台宏的唯一事实来源。
#
# 这一条必须留在 **PRIVATE**，不能改成 PUBLIC —— IDF 会把组件的 PUBLIC 编译选项
# 传播给最终链接目标 `embark_esp32.elf`，而那个目标要编一个自动生成的**纯 C** 文件
# `project_elf_src_esp32s3.c`；embark_config.h 拉进来的 ETL 头是 C++ 专用的，
# 一 include 就是一片 `error: unknown type name 'namespace'`（第二次构建的真实报错）。
# 依赖方（`main/`）自己带同一条 -include，见 platform/esp32/project/main/CMakeLists.txt。
#
# 真机这里**不带 -Wpedantic**（宿主带）：IDF 与 LVGL 的头文件在 -Wpedantic 下会刷一片
# 与我们无关的警告（assert.h 的 #include_next、hal/color_types.h 的匿名结构体、
# lv_table.h 的柔性数组），会淹掉真正该看的行。宿主上这些头是 SYSTEM include
# （LVGL 自己 target_include_directories(... SYSTEM ...)），IDF 侧没有这层区分，
# 所以用"降一档警告级别"换"日志可读"。我们自己的代码在宿主侧仍然受 -Wpedantic 管。
target_compile_options(${COMPONENT_LIB} PRIVATE
        -include "${EMBARK_ROOT}/config/embark_config.h"
        -Wall -Wextra
        -fno-exceptions -fno-rtti)

# 平台宏与版本串（宿主在顶层 CMakeLists.txt 里给 embark_core 定义；真机在这里给）。
# EMBARK_PLATFORM_HAS_FREERTOS=1 ⇒ embark_config.h 会打开 ETL_TARGET_OS_FREERTOS。
# EMBARK_LOG_SERIALIZE=1 ⇒ 日志加锁（真机是多任务环境：UI 任务 + own task + IDF 任务）。
target_compile_definitions(${COMPONENT_LIB} PUBLIC
        EMBARK_PLATFORM_HAS_FREERTOS=1
        EMBARK_LOG_SERIALIZE=1
        EMBARK_PLATFORM_NAME="esp32"
        EMBARK_VERSION_STRING="0.1.0")
