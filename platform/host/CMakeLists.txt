# 宿主平台（spec §11）：HAL 的 Windows 实现 + 宿主可执行文件
#
# embark_platform_host【不】链 embark_kernel_flags：平台后端允许用宿主的标准库
# （fstream / thread / chrono / SDL），-fno-exceptions 只约束内核与 app 那条线。
# 反过来，它是全仓库唯一定义 embark::assert_failed / embark::fatal 的地方（host_system.cpp）。
#
# 分两个库：
#   embark_platform_host     基础后端（时间/持久化/日志/系统/总线 + LVGL 内存钩子）
#                            + UI 任务启动器（ui_task，issues/06）
#   embark_platform_host_ui  SDL2 显示、SDL2 输入（LVGL 端口与 UI 端口在 platform/common，
#                            宿主与真机共用，issues/11）
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
# embark::static_pool：own_task_spawner.h（issue 15）经 pooled_task_spawner.h 用 StaticPool
# 记账槽位，头文件里的调用点落在消费者（ui_demo / ui_tour）的 TU 上，所以符号要从这里透传。
# 真机侧不需要这条：platform/esp32 的组件源列表里已经有 platform/common/static_pool.cpp。
target_link_libraries(embark_platform_host PUBLIC embark::core embark_freertos embark::static_pool)

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
          host_display.cpp
          host_input.cpp)

  add_library(embark::platform_host_ui ALIAS embark_platform_host_ui)

  # SDL_MAIN_HANDLED：我们要普通 main()，不让 SDL.h 把它换成 SDL_main
  # （于是也不必链 SDL2main）。PUBLIC 是给演示程序用的。
  target_compile_definitions(embark_platform_host_ui PUBLIC SDL_MAIN_HANDLED)

  # LVGL 端口与 UI 端口在 platform/common（宿主与真机共用，issues/11），
  # 这里只提供 SDL 的显示/输入后端。
  target_link_libraries(embark_platform_host_ui
          PUBLIC embark::core
                 embark::platform_host
                 embark::common_ui
                 SDL2::SDL2)

  # 演示程序：唯一 UI 任务的完整例子 + 无人值守验收开关
  # （--frames / --click / --drag / --launch / --eez / --screenshot / --shot-frame /
  #   --scale / --delay / --quit-at，全集见 ui_demo.cpp 的 print_usage()）。
  # demo App 本体在 app/（embark::demo_apps），issues/08 起宿主侧不再自带。
  add_executable(embark_host_ui ui_demo.cpp)

  target_link_libraries(embark_host_ui PRIVATE embark::platform_host_ui embark::demo_apps)

  embark_copy_sdl2_runtime(embark_host_ui)

  # 系统用例：不加参数就完整走一遍"启动 → 前台切换（= 武装后台策略）→ 后台节拍 →
  # App 间消息 → 关窗收尾"，每步打一行中文解说，末尾给一张 ✓/✗ 自检清单。
  # 后台策略在 App 第一次被切进前台时才武装（issue 23 / ADR 0009）。
  # 它不是单元测试（不进 ctest），而是给人从启动看到任务切换的可运行说明。
  add_executable(embark_host_tour ui_tour.cpp)

  target_link_libraries(embark_host_tour PRIVATE embark::platform_host_ui embark::demo_apps)

  embark_copy_sdl2_runtime(embark_host_tour)

  # 用户示例入口：干净版"模拟单片机常驻运行"（无验收开关、无合成输入）。
  # 照 user_main.cpp 改 EMBARK_APP_TABLE 换成自己的 App 表即可；
  # main/ui_main 骨架讲解见 docs/index.md 指向的 quickstart 与「写你自己的 main」一节。
  add_executable(embark_host_user user_main.cpp)

  target_link_libraries(embark_host_user PRIVATE embark::platform_host_ui embark::demo_apps)

  embark_copy_sdl2_runtime(embark_host_user)
else()
  message(STATUS "  宿主 UI 目标（embark_host_ui）已跳过：没有 SDL2，显示/输入后端与 LVGL 端口不参与构建。")
endif()
