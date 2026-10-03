# 宿主测试（spec §13）：doctest 单头、编译快。
#
# 注意：测试可执行文件【故意不链】embark_kernel_flags。
# doctest 的 REQUIRE/SUBCASE 需要异常（doctest.h:2818-2852 会把 REQUIRE 换成一个
# static_assert(false, "Exceptions are disabled!")）。"内核无异常"由 embark_core 与
# embark_host 自己的编译选项保证：同一个头文件在那里会被再编一次。

add_executable(embark_tests
        smoke/main.cpp)

target_link_libraries(embark_tests PRIVATE
        embark::core
        doctest::doctest)

add_test(NAME embark_tests COMMAND embark_tests)
