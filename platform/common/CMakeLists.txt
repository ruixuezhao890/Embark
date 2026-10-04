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

add_library(embark_static_pool STATIC static_pool.cpp)

add_library(embark::static_pool ALIAS embark_static_pool)

# 使用者按 "static_pool.h" 引用（真机 esp32_lvgl_mem.cpp 与单元测试都是）。
target_include_directories(embark_static_pool PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(embark_static_pool PUBLIC embark_build_options)

if(NOT TARGET embark_lvgl)
  message(FATAL_ERROR "platform/common 需要 third_party 里的 embark_lvgl"
                      "（add_subdirectory(third_party) 必须在它之前）")
endif()

add_library(embark_common_ui STATIC
        lvgl_port.cpp
        lvgl_ui_port.cpp)

add_library(embark::common_ui ALIAS embark_common_ui)

# 使用者按 "lvgl_port.h" 引用：本目录必须在 include 路径上（宿主与真机都要）。
target_include_directories(embark_common_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# embark::core 提供 IUiPort / hal::Context；embark_lvgl 提供 <lvgl.h>。
target_link_libraries(embark_common_ui PUBLIC embark::core embark_lvgl)
