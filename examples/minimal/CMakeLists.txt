# examples/minimal —— 最小可跑示例：一个 App、一块手绘屏、一个主循环。
#
# 依赖只有两条：
#   embark::platform_host_ui  宿主显示/输入后端 + LVGL 端口 + UI 端口 + UI 任务启动器
#   embark::apps              App 层的编译规矩（-Wall / -fno-exceptions 等）
#
# 故意【不】链 embark::demo_apps：这个例子的意义正是"没有 demo App 也能跑"。
#
# platform/host 的头文件平时只被同目录的源文件用引号包含（ui_demo.cpp / user_main.cpp
# 都是），示例在别的目录，得自己把路径加进来；platform/common 那条会经
# embark::common_ui 自动带过来，一并写上更清楚。

add_executable(embark_example_minimal
        main.cpp
        minimal_app.cpp)

target_include_directories(embark_example_minimal
        PRIVATE "${PROJECT_SOURCE_DIR}/platform/host"
                "${PROJECT_SOURCE_DIR}/platform/common")

target_link_libraries(embark_example_minimal
        PRIVATE embark::platform_host_ui
                embark::apps)

# MinGW 的 SDL2.dll 在 <prefix>/bin，不在系统 PATH 里，复制到 exe 旁边。
embark_copy_sdl2_runtime(embark_example_minimal)
