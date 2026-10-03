# Embark · FreeRTOS 宿主接入（issues/06）
#
# 提供目标 embark_freertos（STATIC）—— 宿主上唯一被构建的 FreeRTOS 内核：
#   * 干净的 upstream V10.6.2（submodule third_party/freertos，不加本地补丁）；
#   * 端口 = portable/MSVC-MingW（WIN32 线程 + 模拟中断，issue 02 的 spike 验证过）；
#   * 配置 = platform/host/freertos/FreeRTOSConfig.h，钩子 = 同目录 freertos_hooks.c
#     （第三方源码零改动，配置与钩子全部自持 —— 与 ETL 的处理方式一致）。
#
# 刻意不编译：
#   * heap_4.c —— configSUPPORT_DYNAMIC_ALLOCATION=0 下内核源码里所有 pvPortMalloc/
#     vPortFree 引用都被 #if 排除（tasks.c / queue.c / event_groups.c / stream_buffer.c
#     实测过），于是 FreeRTOS 堆在"符号层面"就不存在，"0 次动态分配"是结构保证而不是
#     运行统计（config/embark_config.h、spec §10）。
#   * timers.c —— configUSE_TIMERS=0（issue 07 的 etl::callback_timer 由 UI 循环 tick()
#     驱动，不需要软件定时器任务）。
#   * croutine.c —— configUSE_CO_ROUTINES=0。
#   * third_party/freertos/CMakeLists.txt —— 官方内核 CMake 面向通用集成（端口/配置钩子
#     太多），与 Embark 的构建自持约定不符；但保留 include/<middleware> 之外的原始布局，
#     将来换端口不用改这里的路径以外的任何东西。
#
# 副作用（接入后全工程在 FreeRTOS 头文件可见范围内）：
#   * enable_language(C)：测试/宿主目标都是 C++，只有这里需要 C 编译器；
#   * embark_build_options INTERFACE 追加三个 include 目录 —— config/embark_config.h 的
#     ETL_TARGET_OS_FREERTOS=1 是 -include 注入到每个 TU 的，任何 TU 都可能拉 FreeRTOS.h
#     （etl/atomic.h、etl/mutex 系列），所以 include 路径必须全局可见；
#   * MSVC-MingW 端口用 WinMM 的 timeGetTime —— MinGW/GCC 下必须链 winmm，PUBLIC 透传。

if(NOT EMBARK_PLATFORM_HAS_FREERTOS)
  message(STATUS "  FreeRTOS 未接入（EMBARK_PLATFORM_HAS_FREERTOS=OFF）：embark_freertos 不参与构建。")
  return()
endif()

enable_language(C)

set(EMBARK_FREERTOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freertos")
set(EMBARK_FREERTOS_PORT_DIR "${EMBARK_FREERTOS_ROOT}/portable/MSVC-MingW")
set(EMBARK_FREERTOS_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform/host/freertos")

add_library(embark_freertos STATIC
        "${EMBARK_FREERTOS_ROOT}/tasks.c"
        "${EMBARK_FREERTOS_ROOT}/queue.c"
        "${EMBARK_FREERTOS_ROOT}/list.c"
        "${EMBARK_FREERTOS_ROOT}/event_groups.c"
        "${EMBARK_FREERTOS_ROOT}/stream_buffer.c"
        "${EMBARK_FREERTOS_PORT_DIR}/port.c"
        "${EMBARK_FREERTOS_CONFIG_DIR}/freertos_hooks.c"
        # 诊断桥也收进这个归档：configASSERT/栈溢出钩子（C 侧）→ 桥（C++ 侧）→
        # embark::assert_failed / fatal。静态归档内部成员可以互相满足引用；反过来
        # 放平台归档的话，embark_freertos 先扫、平台后扫，tasks.o 的引用就没人喂了。
        "${EMBARK_FREERTOS_CONFIG_DIR}/freertos_bridge.cpp")

target_include_directories(embark_freertos SYSTEM PUBLIC
        "${EMBARK_FREERTOS_ROOT}/include"
        "${EMBARK_FREERTOS_PORT_DIR}"
        "${EMBARK_FREERTOS_CONFIG_DIR}")

set_target_properties(embark_freertos PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON)

# 第三方 C 源码压制警告（同 lvgl 的处理，避免 vendor 代码干扰零警告基线）；
# freertos_hooks.c 是我们自己的，保留警告。
if(MSVC)
  set(EMBARK_FREERTOS_SILENCE "/w")
else()
  set(EMBARK_FREERTOS_SILENCE "-w")
endif()
set_source_files_properties(
        "${EMBARK_FREERTOS_ROOT}/tasks.c"
        "${EMBARK_FREERTOS_ROOT}/queue.c"
        "${EMBARK_FREERTOS_ROOT}/list.c"
        "${EMBARK_FREERTOS_ROOT}/event_groups.c"
        "${EMBARK_FREERTOS_ROOT}/stream_buffer.c"
        "${EMBARK_FREERTOS_PORT_DIR}/port.c"
        PROPERTIES COMPILE_OPTIONS "${EMBARK_FREERTOS_SILENCE}")

# 链接：winmm（MSVC-MingW 端口用 timeGetTime，GCC 下必须显式链）。
# 【不】链 embark_build_options：它的 -include embark_config.h 是 C++ 强制头（ETL 版本
# static_assert、namespace），灌给内核的 C 文件会直接炸。桥的 C++ 侧只需要头文件路径，
# 编译选项在下面按语言单列。
target_link_libraries(embark_freertos PUBLIC winmm)

# 桥的 C++ 侧到工程头的路径。etl::etl 只贡献 include 根（<etl/...>，上游头文件
# 内部按这个名字互相引用），不带任何强制头 —— 对 C 内核文件无害。
target_link_libraries(embark_freertos PRIVATE etl::etl)

# bridge.cpp 的项目头路径（不带任何强制头；include/ = <embark/...>，
# config/ = <embark_limits.h>，build/include = <middleware/...> 视图）。
target_include_directories(embark_freertos PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/config"
        "${CMAKE_BINARY_DIR}/include")

# 只给 C++ 源（= 我们的桥）上工程警告与标准；C 内核文件保持 C11 + 只压警告。
target_compile_options(embark_freertos PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-std=c++17>")

# 全局可见（见文件头注释）。
target_include_directories(embark_build_options INTERFACE
        "${EMBARK_FREERTOS_ROOT}/include"
        "${EMBARK_FREERTOS_PORT_DIR}"
        "${EMBARK_FREERTOS_CONFIG_DIR}")

message(STATUS "  FreeRTOS V10.6.2（submodule）路径：${EMBARK_FREERTOS_ROOT}")