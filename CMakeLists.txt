# Embark —— 可移植嵌入式应用框架（v1）
#
# 顶层构建：宿主（PC 仿真）与 ESP32-S3 两个目标共用同一份源码（spec §11）。
# 规格书：.scratch/embark-v1/spec.md；进度：.scratch/embark-v1/issues/。
#
# 宿主快速上手：
#   cmake -G Ninja -B build
#   cmake --build build
#   ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.24)

project(embark
        VERSION 0.1.0
        DESCRIPTION "Embark —— 单一 UI 任务 + 逻辑 App 的可移植嵌入式框架"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(embark_middleware)

option(EMBARK_BUILD_HOST "构建宿主（PC 仿真）目标" ON)
option(EMBARK_BUILD_TESTS "构建单元测试（doctest）" ON)
option(EMBARK_BUILD_ESP32 "构建 ESP32-S3 目标（尚未实现，见 issues/11）" OFF)

if(EMBARK_BUILD_ESP32)
  message(FATAL_ERROR "ESP32-S3 目标还没实现，见 .scratch/embark-v1/issues/11-esp32s3-backend.md")
endif()
if(NOT EMBARK_BUILD_HOST)
  message(FATAL_ERROR "v1 目前只有宿主目标，EMBARK_BUILD_HOST 必须为 ON")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 默认 Debug：断言打开，行为接近真机调试栈；发布宿主仿真显式传 -DCMAKE_BUILD_TYPE=Release。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# --- 公共编译选项 -----------------------------------------------------------
# 所有 Embark 目标（内核、后端、app、测试）共用：
#   * ETL 等编译期宏统一由 config/embark_config.h 强制注入（-include，见 spec §16.2）
#   * middleware/ 视图只在构建目录里生成（spec §11）
add_library(embark_build_options INTERFACE)
add_library(embark::build_options ALIAS embark_build_options)
target_compile_features(embark_build_options INTERFACE cxx_std_17)

embark_create_middleware_view("${CMAKE_BINARY_DIR}/include/middleware")
target_include_directories(embark_build_options INTERFACE "${CMAKE_BINARY_DIR}/include")

# 平台是否已经提供 FreeRTOS 头文件：决定 config/embark_config.h 是否定义 ETL_TARGET_OS_FREERTOS。
# 宿主 FreeRTOS Windows port 落地（issues/02、06）之前为 0 —— 定义它会让 <etl/atomic.h> /
# <etl/callback_timer.h> 去 include FreeRTOS.h，见 config/embark_config.h 里的实测链条。
# ESP32-S3 后端（issues/11）由 IDF 侧传 1。
set(EMBARK_PLATFORM_HAS_FREERTOS 0 CACHE STRING "平台是否提供 FreeRTOS 头文件（0/1）")
target_compile_definitions(embark_build_options INTERFACE
        EMBARK_PLATFORM_HAS_FREERTOS=${EMBARK_PLATFORM_HAS_FREERTOS})

if(MSVC)
  target_compile_options(embark_build_options INTERFACE
          /W4 /FI"${CMAKE_CURRENT_SOURCE_DIR}/config/embark_config.h")
else()
  target_compile_options(embark_build_options INTERFACE -Wall -Wextra -Wpedantic)
  target_compile_options(embark_build_options INTERFACE
          -include "${CMAKE_CURRENT_SOURCE_DIR}/config/embark_config.h")
endif()

# 内核语言子集：无异常、无 RTTI（spec §10）。
# 测试可执行文件故意不链它 —— doctest 的 REQUIRE/SUBCASE 需要异常，见 tests/CMakeLists.txt。
add_library(embark_kernel_flags INTERFACE)
add_library(embark::kernel_flags ALIAS embark_kernel_flags)
if(MSVC)
  target_compile_options(embark_kernel_flags INTERFACE /EHs-c- /GR-)
else()
  target_compile_options(embark_kernel_flags INTERFACE -fno-exceptions -fno-rtti)
endif()

# --- 目标（顺序：第三方 → 内核 → 平台 → app → 测试） -----------------------
add_subdirectory(third_party)

set(EMBARK_PLATFORM_NAME "host")

add_subdirectory(src)
add_subdirectory(platform/host)
add_subdirectory(app)

if(EMBARK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "Embark ${PROJECT_VERSION} / 平台 ${EMBARK_PLATFORM_NAME} / ${CMAKE_BUILD_TYPE}")
