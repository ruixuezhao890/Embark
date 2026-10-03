# 宿主平台（spec §11）：HAL 的 Windows 实现 + 宿主可执行文件
#
# embark_platform_host【不】链 embark_kernel_flags：平台后端允许用宿主的标准库
# （fstream / thread / chrono / SDL），-fno-exceptions 只约束内核与 app 那条线。
# 反过来，它是全仓库唯一定义 embark::assert_failed / embark::fatal 的地方（host_system.cpp）。
#
# 分两个库：
#   embark_platform_host     基础后端（时间/持久化/日志/系统/总线 + LVGL 内存钩子）
#                            + UI 任务启动器（ui_task，issues/06）
#   embark_platform_host_ui  SDL2 显示、SDL2 输入、LVGL 端口、UI 端口、演示 App
# 找不到 SDL2 时只跳过 UI 那部分：内核、基础后端与全部单元测试照常可建可跑
# （见 cmake/embark_sdl2.cmake）。

# 注意：freertos_bridge.cpp 编进 embark_freertos 而不是这里 —— FreeRTOS 归档
# （tasks.o 的 configASSERT/栈溢出钩子）引用它，静态归档内部成员互相满足；
# 放平台归档会因扫描顺序（平台先、freertos 后）产生未定义引用（embark_freertos.cmake）。
add_library(embark_platform_host STATIC
        host_bus.cpp
        host_context.cpp
        host_log_sink.cpp
        host_lvgl_mem.cpp
        host_persistence.cpp
        host_system.cpp
        host_time.cpp
        ui_task.cpp)

add_library(embark::platform_host ALIAS embark_platform_host)

# PUBLIC：平台后端及其消费者都需要 FreeRTOS（静态库的链接依赖会透传到可执行文件）。
target_link_libraries(embark_platform_host PUBLIC embark::core embark_freertos)

add_executable(embark_host main.cpp)

target_link_libraries(embark_host
        PRIVATE embark::core
                embark::platform_host
                embark::apps
                embark::kernel_flags)

# --- 宿主 UI：SDL2 + LVGL（issues/05）+ UI 端口与演示 App（issues/06）------------
embark_find_sdl2(EMBARK_HOST_SDL2_FOUND)

if(EMBARK_HOST_SDL2_FOUND)
  add_library(embark_platform_host_ui STATIC
          demo_apps.cpp
          host_display.cpp
          host_input.cpp
          lvgl_port.cpp
          lvgl_ui_port.cpp)

  add_library(embark::platform_host_ui ALIAS embark_platform_host_ui)

  # SDL_MAIN_HANDLED：我们要普通 main()，不让 SDL.h 把它换成 SDL_main
  # （于是也不必链 SDL2main）。PUBLIC 是给演示程序用的。
  target_compile_definitions(embark_platform_host_ui PUBLIC SDL_MAIN_HANDLED)

  target_link_libraries(embark_platform_host_ui
          PUBLIC embark::core
                 embark::platform_host
                 embark_lvgl
                 SDL2::SDL2)

  # 演示程序：唯一 UI 任务的完整例子 + 无人值守验收开关
  # （--frames / --click / --switch / --screenshot / --quit-at）。
  add_executable(embark_host_ui ui_demo.cpp)

  target_link_libraries(embark_host_ui PRIVATE embark::platform_host_ui)

  embark_copy_sdl2_runtime(embark_host_ui)
else()
  message(STATUS "  宿主 UI 目标（embark_host_ui）已跳过：没有 SDL2，显示/输入后端与 LVGL 端口不参与构建。")
endif()
