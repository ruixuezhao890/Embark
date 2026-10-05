# 中文字库组件（真机侧，issue 16/17）
#
# 为什么单独开一个组件，而不是把 embark_zh_14.c 塞进 embark 组件：
#   embark 组件对**所有**源文件强制 -include config/embark_config.h（ETL 的编译期宏
#   入口），那个头只能给 C++ 看；而字库是 lv_font_conv 生成的纯 C 文件，
#   套上它就是一片 "unknown type name 'namespace'"。宿主侧同样是单独一个库
#   （platform/common/CMakeLists.txt 的 embark_zh_font），这里保持同一个形状。
#
# 本组件不写任何源文件，只把仓库里的生成物注册给 IDF；字库清单与再生成命令见
# tools/font/embark_zh_14.json（改字要重跑 lv_font_conv 并覆盖 assets/fonts/）。

get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# REQUIRES lvgl：拿 <lvgl.h> 与 LV_CONF_INCLUDE_SIMPLE / LV_LVGL_H_INCLUDE_SIMPLE
# （lvgl 组件把它们定义成 PUBLIC，依赖方自动继承），这样 config/lv_conf.h 会被命中。
idf_component_register(
        SRCS "${EMBARK_ROOT}/assets/fonts/embark_zh_14.c"
        INCLUDE_DIRS "${EMBARK_ROOT}/config"
        REQUIRES lvgl)
