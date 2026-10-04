# ESP32-S3 后端：宿主构建侧的门面（issues/11）
#
# 关键分工先讲清楚：
#   * 真机固件的构建**由 ESP-IDF 自己驱动** —— 真正的工程在 platform/esp32/project/，
#     入口是 `idf.py -C platform/esp32/project -B build-esp32 build`（见 README.md）。
#   * 本文件只属于**宿主 CMake 构建**：顶层 CMakeLists.txt 在 EMBARK_BUILD_ESP32=ON 时
#     add_subdirectory 本目录。它产出的不是库，而是一个"把 IDF 构建串起来"的 custom target，
#     这样 IDE / 顶层构建也能一键触发真机编译（前提是当前环境已经 export 过 IDF）。
#
# 为什么不做成 add_subdirectory 编进去：真机代码依赖 ESP-IDF 的头文件、组件系统与
# xtensa/riscv 工具链，没法用宿主那一套 CMake 直接编（spec §11 的"两条构建路径"）。

add_custom_target(embark_esp32_hint
                  COMMAND ${CMAKE_COMMAND} -E echo
                          "真机构建：idf.py -C platform/esp32/project -B build-esp32 build（先 export.sh/export.ps1）"
                  COMMENT "ESP32-S3 后端构建入口"
                  VERBATIM)

# 环境里已经 export 过 IDF 的话，顺手给出一个真正可用的目标；没 export 就只留上面的提示。
if(DEFINED ENV{IDF_PATH})
  find_program(EMBARK_IDF_PY NAMES idf.py)
  if(EMBARK_IDF_PY)
    add_custom_target(embark_esp32_build
                      COMMAND "${EMBARK_IDF_PY}" -C "${CMAKE_CURRENT_SOURCE_DIR}/project" -B
                              "${CMAKE_BINARY_DIR}/esp32" build
                      COMMENT "idf.py build（ESP32-S3 真机固件）"
                      USES_TERMINAL
                      VERBATIM)
    message(STATUS "ESP32-S3 后端：已接入 idf.py（idf.py=$ENV{IDF_PATH}），目标 embark_esp32_build")
  else()
    message(STATUS "ESP32-S3 后端：IDF_PATH 已设置但 PATH 里没有 idf.py，只提供 embark_esp32_hint")
  endif()
else()
  message(STATUS "ESP32-S3 后端：当前环境没有 IDF_PATH，只提供 embark_esp32_hint（见 platform/esp32/README.md）")
endif()
