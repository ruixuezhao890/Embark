# 宿主测试（spec §13）：doctest 单头、编译快。
#
# 注意：测试可执行文件【故意不链】embark_kernel_flags。
# doctest 的 REQUIRE/SUBCASE 需要异常（doctest.h:2818-2852 会把 REQUIRE 换成一个
# static_assert(false, "Exceptions are disabled!")）。"内核无异常"由 embark_core 与
# embark_host 自己的编译选项保证：同一个头文件在那里会被再编一次。
#
# 也【故意不链】embark_platform_host：测试自己用 core/tests/fakes 搭 HAL 上下文，
# 于是 platform_context() 与平台侧 assert_failed/fatal（core/tests/fakes/fakes.cpp 提供）
# 不会重复定义。
#
# 从 issue 07 起链 embark_freertos：framework.cpp 用 etl::mutex（FreeRTOS 版，
# xSemaphoreCreateMutexStatic → queue.c）与任务创建相关符号，fakes.cpp 已实现
# assert_failed/fatal 把桥接起来；winmm 由 embark_freertos PUBLIC 传递。

add_executable(embark_tests
        smoke/main.cpp
        detail/test_allocation_counter.cpp
        detail/zero_alloc_hooks.cpp
        fakes/fakes.cpp
        hal/test_audio.cpp
        hal/test_bus.cpp
        hal/test_context.cpp
        hal/test_display.cpp
        hal/test_imu.cpp
        hal/test_input.cpp
        hal/test_log_sink.cpp
        hal/test_persistence.cpp
        hal/test_system.cpp
        hal/test_time.cpp
        kernel/test_app_registry.cpp
        kernel/test_launcher_pages.cpp
        kernel/test_framework.cpp
        kernel/test_ui_screen_names.cpp
        kernel/test_font_quota.cpp
        kernel/test_framework_messaging.cpp
        kernel/test_system_tour.cpp
        kernel/test_bus.cpp
        kernel/test_message_queue.cpp
        kernel/test_format_derive.cpp
        kernel/test_static_pool.cpp
        kernel/test_pooled_task_spawner.cpp
        kernel/test_own_task_lifecycle.cpp
        kernel/test_zero_alloc.cpp)

# 测试内部用 "fakes/fakes.h" 这种从 core/tests/ 起算的相对包含。
# app/ 与 app/common/ 也在搜索路径上：屏名解析（app/common/ui_screen_names.h）与字形配额
# 公式（app/common/font_quota.h）都是纯函数，脱离 LVGL 单测。
target_include_directories(embark_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/app"
        "${PROJECT_SOURCE_DIR}/app/common")

 # 音频（issue 09）：被测的是 core/common/wav_player.cpp（宿主与真机共用那一份），
# 它只碰 stdio —— 于是状态机能在 CI 里单测，不需要声卡。
target_link_libraries(embark_tests PRIVATE
        embark::core
        embark::static_pool
        embark::audio
        embark_freertos
        doctest::doctest)

add_test(NAME embark_tests COMMAND embark_tests)

# --- 宿主平台后端的测试（链 embark_platform_host） ---------------------------
# 与 embark_tests 相反：这里要的就是真后端。宿主的平台库不带 core/tests/fakes 的那套替身，
# 单独一个可执行文件不会撞符号。
add_executable(embark_host_tests
        host/test_host_filesystem.cpp
        host/test_network.cpp
        host/test_text_wrap.cpp
        host/test_ttf_font.cpp)

target_include_directories(embark_host_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/host")

# 字体用例要真文件：仓库里那份 ui_heiti.ttf（ADR 0017 之后字体只有这一条路）。
# 缺了就在配置期失败 —— "字体链在 CI 上偷偷坏掉而测试全绿"比早报错贵得多。
set(EMBARK_TEST_TTF_FILE "${PROJECT_SOURCE_DIR}/app/aidogui_ui/assets/font/ui_heiti.ttf")
if(NOT EXISTS "${EMBARK_TEST_TTF_FILE}")
  message(FATAL_ERROR "字体用例需要 ${EMBARK_TEST_TTF_FILE}（见 ADR 0017）")
endif()
target_compile_definitions(embark_host_tests PRIVATE "EMBARK_TEST_TTF_FILE=\"${EMBARK_TEST_TTF_FILE}\"")

# 链 embark::common_ui：字体用例要 LVGL 与 tiny_ttf，而 LV_FONT_DEFAULT == &embark_zh_14
# ⇒ 必须连带静态子集字库（whole-archive 那条规矩在 embark_common_ui 自己的链接接口上，
# 这里不重复）。文件系统用例仍然只用到平台后端。
# liblvgl.a 要的 embark_lvgl_alloc / embark_lvgl_assert_failed 定义在 platform_host 的
# host_lvgl_mem.cpp 里，而归档按命令行顺序只扫一遍。正式程序里 aidogui 桥会调
# font_alloc_scope_begin()，正好把 host_lvgl_mem.o 提前拖进来，所以从来不疼；这个测试
# 只碰 lv_tiny_ttf，谁也没提前引用它 —— 整包进（whole-archive）是这里最省事的解法。
target_link_libraries(embark_host_tests PRIVATE
        -Wl,--whole-archive
        embark::platform_host
        -Wl,--no-whole-archive
        embark::common_ui
        doctest::doctest)

add_test(NAME embark_host_tests COMMAND embark_host_tests)
