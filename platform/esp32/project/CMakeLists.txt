# Embark ESP32-S3 工程（ESP-IDF 顶层 CMakeLists）
#
# 与宿主构建的关系：这个工程**不通过**仓库顶层 CMakeLists 配置，而是由 IDF 自己驱动 ——
#
#   idf.py -C platform/esp32/project -B build-esp32 set-target esp32s3
#   idf.py -C platform/esp32/project -B build-esp32 build
#
# 源码不复制进工程：components/embark 与 components/lvgl 两个组件用绝对路径
# 指向仓库根下的 src/、app/、platform/common/、platform/esp32/src/ 与 third_party/lvgl
# （spec §11 的"一份源码树"）。
#
# 这里要在 project() 之前做一件事：造 middleware/ 视图。理由见 cmake/embark_middleware.cmake ——
# 上游 efmt/elog 的头文件里写死了 <middleware/...> 路径，视图得在组件开始配置之前就存在
# （IDF 的组件配置发生在 project() 里）。注意第二参数显式传仓库根：IDF 工程的
# PROJECT_SOURCE_DIR 是 platform/esp32/project，不是仓库根。

cmake_minimum_required(VERSION 3.16)

set(EMBARK_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(EMBARK_REPO_ROOT "${EMBARK_REPO_ROOT}" ABSOLUTE)

include("${EMBARK_REPO_ROOT}/cmake/embark_middleware.cmake")
embark_create_middleware_view("${CMAKE_BINARY_DIR}/include/middleware" "${EMBARK_REPO_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(embark_esp32)
