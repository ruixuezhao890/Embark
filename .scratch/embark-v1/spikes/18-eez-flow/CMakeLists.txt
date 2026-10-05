# Spike 18：EEZ Flow × Embark LVGL 8.3.11（宿主，最小独立构建）
# 不进仓库骨架（照 issue 02 先例）；依赖 vendor/ 样例的三个 submodule 目录。
cmake_minimum_required(VERSION 3.24)
project(spike18_eez_flow LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)   # lvgl 的 install 规则会触发 CMP0177（仓库同款压制）

# spike 位于 <repo>/.scratch/embark-v1/spikes/18-eez-flow → 上溯 5 层到仓库根
get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(VENDOR "${EMBARK_ROOT}/.scratch/embark-v1/vendor/native-interface-lvgl-with-flow")
set(EEZ_DIR "${VENDOR}/eez-framework")            # pin c3e0ac0
set(LV_DRIVERS_DIR "${VENDOR}/lv_drivers")        # pin 8cdabe8d（release/v8.3）

# ---------- LVGL 8.3.11（仓库 submodule 74d0a81）+ spike 自己的 lv_conf.h ----------
set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "spike lv_conf.h" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
add_subdirectory("${EMBARK_ROOT}/third_party/lvgl" lvgl_build EXCLUDE_FROM_ALL)

# ---------- EEZ Flow 运行时（MIT，压制第三方警告） ----------
file(GLOB_RECURSE EEZ_SRC "${EEZ_DIR}/src/eez/*.cpp" "${EEZ_DIR}/src/eez/*.c")
list(FILTER EEZ_SRC EXCLUDE REGEX "platform/stm32|libs/libscpi")
add_library(eez_framework STATIC ${EEZ_SRC})
target_include_directories(eez_framework SYSTEM PUBLIC
    "${EEZ_DIR}/src"
    "${EEZ_DIR}/src/eez/libs/agg"
    "${EEZ_DIR}/src/eez/platform/simulator")
target_compile_definitions(eez_framework PUBLIC EEZ_FOR_LVGL EEZ_PLATFORM_SIMULATOR)
target_compile_options(eez_framework PRIVATE -w)
target_link_libraries(eez_framework PUBLIC lvgl::lvgl)
if(WIN32)
    target_compile_definitions(eez_framework PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

# ---------- lv_drivers（只留 SDL 显示器驱动） ----------
file(GLOB_RECURSE LV_DRIVERS_SRC "${LV_DRIVERS_DIR}/*.c")
list(FILTER LV_DRIVERS_SRC EXCLUDE REGEX "wayland|gtkdrv|win32drv|win_drv[.]c")
add_library(lvgl_drivers STATIC ${LV_DRIVERS_SRC})
target_include_directories(lvgl_drivers SYSTEM PUBLIC "${LV_DRIVERS_DIR}")
target_link_libraries(lvgl_drivers PUBLIC lvgl::lvgl)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(lvgl_drivers PUBLIC SDL2::SDL2)

# ---------- 可执行：main + hooks + EEZ 生成 UI 代码 ----------
file(GLOB UI_C "${CMAKE_CURRENT_LIST_DIR}/ui/*.c")
file(GLOB UI_CPP "${CMAKE_CURRENT_LIST_DIR}/ui/*.cpp")
add_executable(spike18 main.c hooks.c ${UI_C} ${UI_CPP})
# 生成代码写 <lvgl/lvgl.h>：include 根=third_party（lvgl.h 在那）
target_include_directories(spike18 SYSTEM PRIVATE "${EMBARK_ROOT}/third_party")
target_link_libraries(spike18 PRIVATE lvgl_drivers eez_framework lvgl::lvgl SDL2::SDL2)
if(WIN32)
    target_link_libraries(spike18 PRIVATE wsock32 ws2_32)
endif()

# SDL2.dll 拷到可执行文件旁（MinGW 的 DLL 不在系统 PATH）
if(WIN32)
    get_target_property(_sdl2_loc SDL2::SDL2 IMPORTED_LOCATION_RELEASE)
    if(NOT _sdl2_loc)
        get_target_property(_sdl2_loc SDL2::SDL2 IMPORTED_LOCATION)
    endif()
    get_filename_component(_sdl2_dir "${_sdl2_loc}" DIRECTORY)
    if(EXISTS "${_sdl2_dir}/SDL2.dll")
        add_custom_command(TARGET spike18 POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_sdl2_dir}/SDL2.dll" "$<TARGET_FILE_DIR:spike18>/SDL2.dll")
    endif()
endif()