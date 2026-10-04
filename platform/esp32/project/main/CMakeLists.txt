# main 组件：只是入口（app_main → 起唯一 UI 任务），真正的代码都在 embark 组件里。
#
# REQUIRES embark 是为了拿到它的 PUBLIC 头文件（embark/*.h、demo_apps.h 等），
# 以及它 **PUBLIC** 传下来的 `-include config/embark_config.h`
# （ETL 锁宏、日志开关、平台宏的唯一开关；少了它 callback_timer.h 会直接 #error）。
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES embark)

# 入口 TU 也按内核口径编译：C++17、无异常、无 RTTI —— 与 embark 组件同一套语言口径，
# 免得同一个头文件在两个 TU 里按不同标准/异常模型编出不一致的 inline 代码。
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_EXTENSIONS OFF)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

# config/embark_config.h 强制包含：ETL 锁宏 / 日志开关 / 平台宏的唯一开关，必须在任何
# 头文件之前进来（少了它 framework.h → etl/callback_timer.h 直接 `#error`）。
# 这里**不能靠 embark 组件的 PUBLIC 选项传播**：IDF 会把 PUBLIC 选项也加到最终链接目标
# `embark_esp32.elf` 上，而那个目标要编一个自动生成的纯 C 文件 —— 那个 C 文件看见
# embark_config.h 里的 ETL 头会直接炸（`unknown type name 'namespace'`）。
get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
target_compile_options(${COMPONENT_LIB} PRIVATE -include "${EMBARK_ROOT}/config/embark_config.h")
