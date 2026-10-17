# 字体运行时栅格化 spike（.scratch/embark-v1/spikes/font-runtime/）
#
# 只由根 CMakeLists 的 EMBARK_BUILD_FONT_SPIKE 开关拉进来（默认关），不进产品路径。
# 独立可执行文件：不链 platform_host（那个库自带 main 与 SDL 窗口），
# 自己实现 embark_lvgl_* 四个钩子 + 一块 240×320 RGB565 的假屏（对齐真机
# 「绘制缓冲 40 行」的配置，见 platform/esp32/src/esp32_lvgl_port.cpp）。
add_executable(embark_font_spike font_spike.cpp)

target_link_libraries(embark_font_spike PRIVATE embark_lvgl embark_build_options)

# config/lv_conf.h 把 LV_FONT_DEFAULT 设成 &embark_zh_14，于是 liblvgl.a 的
# lv_theme.c / lv_draw_label.c 里有对它的未定义引用。GNU ld 只在「扫描到该归档时
# 符号仍未定义」才拉成员，顺序不由我们决定 —— 和 platform/common/CMakeLists.txt
# 一样用 whole-archive 消掉这个问题。
target_link_libraries(embark_font_spike PRIVATE
        "-Wl,--whole-archive" embark_zh_font "-Wl,--no-whole-archive")
