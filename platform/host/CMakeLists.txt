# 宿主后端（PC 仿真）
#
# 目标形状：SDL2 显示/输入 + 宿主 FreeRTOS port + LVGL（spec §12、§14）。
# 骨架期只有平台占位目标与入口可执行文件；真正的后端实现按 issues/02、05、06 补。

add_library(embark_platform_host INTERFACE)
add_library(embark::platform_host ALIAS embark_platform_host)
target_link_libraries(embark_platform_host INTERFACE embark_build_options)
target_compile_definitions(embark_platform_host INTERFACE EMBARK_PLATFORM_HOST=1)

# 宿主可执行文件：内核 + 本平台后端，保持内核语言子集（无异常、无 RTTI）
add_executable(embark_host main.cpp)
target_link_libraries(embark_host PRIVATE
        embark::core
        embark::platform_host
        embark::kernel_flags)
