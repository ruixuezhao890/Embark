# core/examples/ —— 自包含示例（不依赖 app/ 里的 demo App，也不依赖 AiDoGUI Designer）
#
# 每个子目录一个独立可执行文件。共同前提是宿主 UI 那条线已经建起来
# （SDL2 + LVGL + core/common 的端口）；没有 SDL2 就整体跳过，与
# host 里宿主 UI 目标的处理一致。
#
# 与 host/user_main.cpp 的分工：
#   user_main.cpp      「干净版演示入口」——App 表里是 demo 三件套
#   core/examples/minimal/  「从零写一个 App」的完整最小样本 —— 一个 App + 一块手绘屏 + 一个主循环

if(TARGET embark::platform_host_ui)
  add_subdirectory(minimal)
else()
  message(STATUS "  core/examples/ 已跳过：没有 SDL2（embark::platform_host_ui 未构建）。")
endif()
