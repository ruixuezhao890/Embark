# AiDoGUI 导出代码组件（真机侧，ADR 0011）
#
# 与宿主 app/aidogui_ui/CMakeLists.txt 同一个口径，只是换成 IDF 组件：
#   * 源：app/aidogui_ui/export 下的 *.c + screens/ + components/ + events/ + custom/ +
#     animations/ + fonts/ 的 *.c（设计器导出的纯 C）。字模本体是 SD 卡上的 .bin 运行时加载
#     （ADR 0017），export/fonts/ui_fonts.c 只是那批 lv_font_t* 指针与加载实现 —— 但它必须编，
#     屏代码直接引用指针符号。
#   * 表头：core/cmake/embark_aidogui_tables.cmake 在**配置期**解析 export/，写出
#     embark_aidogui_ui_tables.h（屏表 / 控件表 / 字体表），落在本组件的 build include 根，
#     由 app/common/aidogui_ui_bridge.cpp 与 aidogui_ui_nav.cpp 使用。
#   * 强制包含 app/common/aidogui_ui_hooks.h：生成代码里每个屏的「点 App 图标打开」钩子
#     在那份头里声明（embark_ui_open_<app>）。
#
# 为什么单开一个组件而不并进 embark 组件：
#   ① 这批生成代码是**纯 C**，而 embark 组件的每个 TU 都吃 -include core/config/embark_config.h
#      （C++ 专用的 ETL 头）—— 混进去就是一片 `unknown type name 'namespace'`，与
#      components/embark_font 单开一个组件是同一个理由；
#   ② 设计器产物一律 -w 静音（它不按本仓库的 -Wall -Wextra 口径走）；
#   ③ export 目录是设计器的输出面，GLOB 带 CONFIGURE_DEPENDS，重导出后自动重配。
#
# 组件目录在 <repo>/device/project/components/embark_aidogui，仓库根上跳 4 级。
get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(EMBARK_AIDOGUI_EXPORT_DIR "${EMBARK_ROOT}/app/aidogui_ui/export")

# IDF 收集组件依赖时会先用 cmake -P 跑一遍本文件：脚本模式下 file(GLOB ... CONFIGURE_DEPENDS)
# 会直接报 "CONFIGURE_DEPENDS invalid"，表生成也没有构建目录可用 —— 所以脚本模式只给一份
# 够 register 的最小源清单，真配置（非脚本模式）才做完整 GLOB + 表生成。
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB EMBARK_AIDOGUI_SOURCES "${EMBARK_AIDOGUI_EXPORT_DIR}/*.c")
else()
    # 与宿主 app/aidogui_ui/CMakeLists.txt 同一份目录清单（七个），一个都不能漏：
    #   * events/ —— 屏的 init 里调 aido_event_init_<app>()，漏了就是一片 undefined reference；
    #   * fonts/ —— ui_fonts.c 只定义 lv_font_t* 指针和一个从 .bin 加载的实现（ADR 0017，
    #     5 KB，不含字模），但屏代码直接引用 ui_font_ui_heiti_* 这些符号，漏了照样链接失败。
    # 两处都是 2026-11 首次真机构建（顶层重排后）依次暴露出来的。
    file(GLOB EMBARK_AIDOGUI_SOURCES CONFIGURE_DEPENDS
            "${EMBARK_AIDOGUI_EXPORT_DIR}/*.c"
            "${EMBARK_AIDOGUI_EXPORT_DIR}/screens/*.c"
            "${EMBARK_AIDOGUI_EXPORT_DIR}/components/*.c"
            "${EMBARK_AIDOGUI_EXPORT_DIR}/events/*.c"
            "${EMBARK_AIDOGUI_EXPORT_DIR}/custom/*.c"
            "${EMBARK_AIDOGUI_EXPORT_DIR}/animations/*.c"
            "${EMBARK_AIDOGUI_EXPORT_DIR}/fonts/*.c")
endif()

if(NOT EMBARK_AIDOGUI_SOURCES)
    message(WARNING "embark_aidogui：${EMBARK_AIDOGUI_EXPORT_DIR} 下没有 C 源 —— "
                    "先用 GUI 导出一次，或跑 pwsh -NoProfile -File tools/aidogui/export.ps1")
endif()

# 表头落在 <build>/esp-idf/embark_aidogui/include；IDF 在 register 时**校验**这个目录存在
#（component.cmake:317），而表是在 register 之后才生成的 —— 所以先建目录。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")

# REQUIRES 写成一行：IDF 会把它抄进 build_properties.temp.cmake，跨行/夹注释会让那份
# 生成文件报 "Argument not separated from preceding token"（见 components/embark）。
#
# INCLUDE_DIRS 两个：
#   ① 本组件的 build include 根 —— 生成出来的 embark_aidogui_ui_tables.h 落在那里；
#   ② 导出目录本身 —— 生成代码自己的头（aidogui_objects.h / ui.h / ui_helpers.h …）躺在
#      export/ 下，而表头里写着 #include <aidogui_objects.h>。宿主那边这条由
#      app/aidogui_ui/CMakeLists.txt 的 target_include_directories 给出；IDF 侧没有那条线，
#      只能在这里给。漏了就是 embark 组件编 aidogui_ui_bridge.cpp 时报
#      "fatal error: aidogui_objects.h: No such file or directory"（2026-11 首次真机构建暴露）。
idf_component_register(
        SRCS ${EMBARK_AIDOGUI_SOURCES}
        INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/include"
                     "${EMBARK_AIDOGUI_EXPORT_DIR}"
        REQUIRES lvgl)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    include("${EMBARK_ROOT}/core/cmake/embark_aidogui_tables.cmake")
    embark_generate_aidogui_tables(
            "${EMBARK_AIDOGUI_EXPORT_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/include/embark_aidogui_ui_tables.h")

    # -w：设计器产物静音；-include 钩子头：生成代码里的 embark_ui_open_<app>() 声明来自它
    # （宿主那份靠 app/aidogui_ui/CMakeLists.txt 的 target_compile_options 带进来）。
    target_compile_options(${COMPONENT_LIB} PRIVATE
            -w
            -include "${EMBARK_ROOT}/app/common/aidogui_ui_hooks.h")
endif()
