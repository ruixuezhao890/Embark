# 宿主测试（spec §13）：doctest 单头、编译快。
#
# 注意：测试可执行文件【故意不链】embark_kernel_flags。
# doctest 的 REQUIRE/SUBCASE 需要异常（doctest.h:2818-2852 会把 REQUIRE 换成一个
# static_assert(false, "Exceptions are disabled!")）。"内核无异常"由 embark_core 与
# embark_host 自己的编译选项保证：同一个头文件在那里会被再编一次。
#
# 也【故意不链】embark_platform_host：测试自己用 tests/fakes 搭 HAL 上下文，
# 于是 platform_context() 与平台侧 assert_failed/fatal（tests/fakes/fakes.cpp 提供）
# 不会重复定义。

add_executable(embark_tests
        smoke/main.cpp
        fakes/fakes.cpp
        hal/test_bus.cpp
        hal/test_display.cpp
        hal/test_input.cpp
        hal/test_log_sink.cpp
        hal/test_persistence.cpp
        hal/test_system.cpp
        hal/test_time.cpp)

# 测试内部用 "fakes/fakes.h" 这种从 tests/ 起算的相对包含。
target_include_directories(embark_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(embark_tests PRIVATE
        embark::core
        doctest::doctest)

add_test(NAME embark_tests COMMAND embark_tests)
