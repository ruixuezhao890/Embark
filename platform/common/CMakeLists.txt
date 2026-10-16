# 平台共用层（spec §11）：宿主与真机共用、且**不认平台类型**的胶水。
#
# 两个目标（有意分开）：
#   embark_static_pool  定容静态内存池（issues/11）：不依赖 LVGL，单元测试直接链它
#   embark_common_ui    LVGL 端口 + UI 端口：只认 HAL 与 LVGL，display/input/时间/日志
#                       全从 hal::Context 取 —— 于是宿主（SDL）与真机（esp_lcd + CST328）
#                       共用同一份代码，切后端时 app/ 与 include/embark/ 一行不改（§14.5）。
#
# 本目录不定义 embark::assert_failed / embark::fatal —— 那是各平台后端的责任
# （宿主 = platform/host/host_system.cpp，真机 = platform/esp32/src/esp32_system.cpp）。

add_library(embark_static_pool STATIC ${EMBARK_COMMON_STATIC_POOL_SOURCES})

add_library(embark::static_pool ALIAS embark_static_pool)

# 使用者按 "static_pool.h" 引用（真机 esp32_lvgl_mem.cpp 与单元测试都是）。
target_include_directories(embark_static_pool PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(embark_static_pool PUBLIC embark_build_options)

if(NOT TARGET embark_lvgl)
  message(FATAL_ERROR "platform/common 需要 third_party 里的 embark_lvgl"
                      "（add_subdirectory(third_party) 必须在它之前）")
endif()

# 源清单取自 cmake/embark_sources.cmake（宿主与真机共用，issue 20）。
# EMBARK_COMMON_UI_SOURCES = 两侧都编（LVGL 端口 + UI 端口）；
# EMBARK_COMMON_UI_HOST_ONLY_SOURCES = 只有宿主编 —— font_service.cpp（运行时字库服务，
# issue 17）把 LVGL 的 'F' 盘接到 hal::IFileSystem，真机 Context::filesystem == nullptr
# 时是死代码，等 SD 后端（issue 25）落地再从清单里挪到共用那一份。
add_library(embark_common_ui STATIC
        ${EMBARK_COMMON_UI_SOURCES}
        ${EMBARK_COMMON_UI_HOST_ONLY_SOURCES})

add_library(embark::common_ui ALIAS embark_common_ui)


# 中文字库：静态子集字库（lv_font_conv 生成；清单 tools/font/embark_zh_14.json）。
# 单独成库、按纯 C 编译：embark_build_options 的强制 -include embark_config.h 会把
# C++ 头（etl）塞进 C 编译（unknown type name namespace），而这个 TU 只需要 lvgl.h。
add_library(embark_zh_font STATIC ${EMBARK_FONT_SOURCES})
target_include_directories(embark_zh_font PRIVATE
        "${PROJECT_SOURCE_DIR}/config"
        "${PROJECT_SOURCE_DIR}/third_party/lvgl")
target_compile_definitions(embark_zh_font PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
# 使用者按 "lvgl_port.h" 引用：本目录必须在 include 路径上（宿主与真机都要）。
target_include_directories(embark_common_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# embark::core 提供 IUiPort / hal::Context；embark_lvgl 提供 <lvgl.h>。
target_link_libraries(embark_common_ui PUBLIC embark::core embark_lvgl)

# 静态子集字库：只参与最终链接（embark_common_ui 的所有消费者都会带上它）。
# 必须用 --whole-archive 包住：config/lv_conf.h 把默认字体设成 LV_FONT_DEFAULT ==
# &embark_zh_14，于是 liblvgl.a 里的 lv_theme.c.obj / lv_draw_label.c.obj 会引用它，
# 而 GNU ld 只在「扫描到该归档时该符号仍未定义」才把成员拉进来 —— 各目标的依赖链
# 顺序不由我们决定（实测 liblvgl.a 排在字库归档之后，报 undefined reference to
# `embark_zh_14'，2026-10-16）。whole-archive 让成员无条件进链接，与顺序无关；
# 真机侧对应的是 idf_component_register(... WHOLE_ARCHIVE)。
target_link_libraries(embark_common_ui PRIVATE
        "-Wl,--whole-archive" embark_zh_font "-Wl,--no-whole-archive")


# AiDoGUI 资源盘（ADR 0011）：AiDoGUI 生成代码把字体/图片路径写死成 S:/fonts/*.bin 与
# S:/images/*.bin（模板行为，与工程设置无关）。本目标注册 LVGL 的 'S' 盘，把它指向一个
# 真实目录。不用 LVGL 自带的 LV_USE_FS_POSIX：它的根是编译期宏（LV_FS_POSIX_PATH），
# 而宿主（仓库目录）与真机（SD 卡 / FATFS 挂载点）的根不同，开发期还要能临时换根。
# 默认根 = 仓库里的 app/aidogui_ui/export，也就是 AiDoGUI 导出目录本身（运行时可用环境变量 EMBARK_AIDOGUI_RES
# 覆盖；真机 step 3 指到 esp_vfs_fat 的挂载点即可，两边代码同一份）。
add_library(embark_aidogui_res STATIC ${EMBARK_COMMON_AIDOGUI_RES_SOURCES})
add_library(embark::aidogui_res ALIAS embark_aidogui_res)
target_include_directories(embark_aidogui_res PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(embark_aidogui_res PUBLIC embark_lvgl)
target_compile_definitions(embark_aidogui_res PRIVATE
        "EMBARK_AIDOGUI_RES_ROOT=\"${PROJECT_SOURCE_DIR}/app/aidogui_ui/export\"")
