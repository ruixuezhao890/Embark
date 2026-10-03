# Embark issue 02 一次性探针：宿主上跑真 FreeRTOS（Windows/MSVC-MingW 端口）
# 归档工程只读引用，不改动；本工程不进仓库，结论回填 .scratch/embark-v1/issues/02-*.md
cmake_minimum_required(VERSION 3.20)

project(embark_freertos_host_spike LANGUAGES C CXX)

set(FREERTOS_DIR "E:/01_Workspace/01_Archives/lvgl_template_laste/freertos_/kernel"
    CACHE PATH "归档 FreeRTOS kernel（只读）")

add_library(freertos_kernel STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/MSVC-MingW/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/hooks.c
)
target_include_directories(freertos_kernel PUBLIC
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/MSVC-MingW
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(freertos_kernel PRIVATE c_std_11)
# MSVC-MingW 端口在 GCC 分支下 #include "mmsystem.h" → 必须链 winmm
target_link_libraries(freertos_kernel PUBLIC winmm)

add_executable(spike main.cpp)
target_compile_features(spike PRIVATE cxx_std_17)
target_compile_options(spike PRIVATE -fno-exceptions -fno-rtti)
target_link_libraries(spike PRIVATE freertos_kernel)

# 附带的计时探针：解释 1 tick 实测 2 ms 的来源（不依赖 FreeRTOS）
add_executable(sleepprobe sleepprobe.cpp)
target_compile_features(sleepprobe PRIVATE cxx_std_17)
target_link_libraries(sleepprobe PRIVATE winmm)
