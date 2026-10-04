# LVGL 组件（真机侧，issues/11）
#
# 为什么不用 LVGL 自带的 env_support/cmake/esp.cmake：
#   * 它把 third_party/（lvgl 的上级目录）也塞进 include 根，污染命名空间；
#   * 它依赖 CONFIG_LV_* 这一整套 Kconfig，而我们的配置走仓库里的 config/lv_conf.h
#     （与宿主同一份 —— 宿主侧用 LV_CONF_PATH 缓存变量指过去，真机侧靠 include 路径），
#     这样"切后端不改配置"这件事才成立。
# 所以这里手写一份等价但更小的注册逻辑（同样 GLOB_RECURSE 全量 src/*.c，不做排除）。

get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(EMBARK_LVGL_DIR "${EMBARK_ROOT}/third_party/lvgl")

if(NOT EXISTS "${EMBARK_LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "找不到 LVGL 源码：${EMBARK_LVGL_DIR}（先 git submodule update --init --recursive）")
endif()

file(GLOB_RECURSE EMBARK_LVGL_SOURCES "${EMBARK_LVGL_DIR}/src/*.c")

idf_component_register(
        SRCS ${EMBARK_LVGL_SOURCES}
        INCLUDE_DIRS
             "${EMBARK_LVGL_DIR}"        # lvgl.h
             "${EMBARK_LVGL_DIR}/src"    # 内部头
             "${EMBARK_ROOT}/config"     # 让 #include "lv_conf.h" 命中 config/lv_conf.h
        REQUIRES esp_timer)

# LV_CONF_INCLUDE_SIMPLE 让 LVGL 用 #include "lv_conf.h" 找配置（默认是相对路径）。
# LV_LVGL_H_INCLUDE_SIMPLE 让 lvgl 内部用 #include "lvgl.h"（而不是 ../../lvgl.h）。
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)

# 第三方代码不参与我们自己的告警门禁（宿主侧对 LVGL 也是这么处理的）。
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
