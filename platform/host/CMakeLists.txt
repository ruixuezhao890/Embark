# 宿主平台（spec §11）：HAL 的 Windows 实现 + 宿主可执行文件
#
# 这个库【不】链 embark_kernel_flags：平台后端允许用宿主的标准库（fstream / thread /
# chrono），-fno-exceptions 只约束内核与 app 那条线。反过来，它必须定义
# embark::assert_failed / embark::fatal（host_system.cpp），链接期唯一。
#
# 显示与输入后端（SDL2 + LVGL）按 issues/05 加进来。

add_library(embark_platform_host STATIC
        host_bus.cpp
        host_context.cpp
        host_log_sink.cpp
        host_persistence.cpp
        host_system.cpp
        host_time.cpp)

add_library(embark::platform_host ALIAS embark_platform_host)

target_link_libraries(embark_platform_host PUBLIC embark::core)

add_executable(embark_host main.cpp)

target_link_libraries(embark_host
        PRIVATE embark::core
                embark::platform_host
                embark::apps
                embark::kernel_flags)
