# Embark 内核组件（真机侧，issues/11）
#
# 这个组件**不复制任何源码**：SRCS / INCLUDE_DIRS 全用绝对路径指回仓库根，与宿主构建
# 共用同一份 src/、app/、platform/common/、platform/esp32/src/（spec §11 的"一份源码树，
# 两条构建路径"）。组件目录在 <repo>/platform/esp32/project/components/embark，
# 所以仓库根要上跳 5 级。

# 只用于找到下面那份清单文件；仓库根 / 各目录的路径都在清单里定义。
get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# 源清单的唯一事实来源（issue 20 的结构性收口）：这个组件以前手抄了四份名单
# （内核 / Demo App / 共用胶水 / ESP32 后端）+ 一份 App include 目录 —— issue 16 漏了
# nav_shell.cpp 就在链接期 undefined reference，2026-10-16 漏了三个 App 目录就在编译期
# No such file or directory。现在名单只有 cmake/embark_sources.cmake 一处：
#   EMBARK_APP_SOURCES_DEVICE  真机要编的 App 子集（⊆ 宿主全量，审计会查）
#   EMBARK_APP_INCLUDE_DIRS_DEVICE  由那子集推导（新 App 自动带上自己的目录）
#   EMBARK_COMMON_SOURCES_DEVICE    共用胶水 − 显式声明的宿主专属（减法，不是手抄）
# include 它同时会跑一次配置期审计，漏项在那里断。
include("${EMBARK_ROOT}/cmake/embark_sources.cmake")

idf_component_register(
        SRCS ${EMBARK_KERNEL_SOURCES}
             ${EMBARK_APP_SOURCES_DEVICE}
             ${EMBARK_COMMON_SOURCES_DEVICE}
             ${EMBARK_ESP32_SOURCES}
        INCLUDE_DIRS
             "${EMBARK_ROOT}/include"              # embark/*.h
             "${EMBARK_ROOT}/config"               # embark_config.h / embark_limits.h / lv_conf.h
             "${EMBARK_ROOT}/platform/common"      # lvgl_port.h / lvgl_ui_port.h / static_pool.h
             "${EMBARK_ROOT}/platform/esp32/src"   # esp32_*.h
             "${CMAKE_BINARY_DIR}/include"         # middleware 视图（<middleware/etl|efmt|elog/...>）
             "${EMBARK_ROOT}/third_party/etl/include"  # <etl/...>（ETL 是 header-only）
             ${EMBARK_APP_INCLUDE_DIRS_DEVICE}     # app/、app/common/ 与各 App 目录（推导）
        # fatfs / sdmmc / esp_driver_sdspi|sdmmc|spi：SD 卡（issue 25）用 SPI 模式挂 FAT，
        # 走 esp_vfs_fat_sdspi_mount（driver/sdspi_host.h、driver/sdmmc_host.h、
        # driver/spi_master.h）。板级引脚默认没填，但驱动代码照样编译（见 esp32_sd.cpp）。
        # 注意 REQUIRES 写成**一行**：IDF 会把它们抄进 build_properties.temp.cmake，
        # 跨行/夹注释会让那份生成文件报 "Argument not separated from preceding token"。
        # embark_aidogui：AiDoGUI 导出代码（生成代码 + 配置期写出的 aidogui 表头）。
        # 它只会在启用真机 UI 后端时被用到，但组件注册是静态的 —— 一直是依赖，走 GLOB，
        # export/ 被重导出时自动重配。
        REQUIRES lvgl embark_aidogui embark_font nvs_flash esp_timer esp_lcd driver fatfs sdmmc esp_driver_sdspi esp_driver_sdmmc esp_driver_spi)

# 内核与 App 的编译口径（与宿主 embark_kernel_flags 对齐：C++17、无异常、无 RTTI）。
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_EXTENSIONS OFF)

# config/embark_config.h 强制包含（宿主侧是 -include .../config/embark_config.h）：
# 它是 ETL 配置、日志开关、平台宏的唯一事实来源。
#
# 这一条必须留在 **PRIVATE**，不能改成 PUBLIC —— IDF 会把组件的 PUBLIC 编译选项
# 传播给最终链接目标 `embark_esp32.elf`，而那个目标要编一个自动生成的**纯 C** 文件
# `project_elf_src_esp32s3.c`；embark_config.h 拉进来的 ETL 头是 C++ 专用的，
# 一 include 就是一片 `error: unknown type name 'namespace'`（第二次构建的真实报错）。
# 依赖方（`main/`）自己带同一条 -include，见 platform/esp32/project/main/CMakeLists.txt。
#
# 真机这里**不带 -Wpedantic**（宿主带）：IDF 与 LVGL 的头文件在 -Wpedantic 下会刷一片
# 与我们无关的警告（assert.h 的 #include_next、hal/color_types.h 的匿名结构体、
# lv_table.h 的柔性数组），会淹掉真正该看的行。宿主上这些头是 SYSTEM include
# （LVGL 自己 target_include_directories(... SYSTEM ...)），IDF 侧没有这层区分，
# 所以用"降一档警告级别"换"日志可读"。我们自己的代码在宿主侧仍然受 -Wpedantic 管。
target_compile_options(${COMPONENT_LIB} PRIVATE
        -include "${EMBARK_ROOT}/config/embark_config.h"
        -Wall -Wextra
        -fno-exceptions -fno-rtti)

# 平台宏与版本串（宿主在顶层 CMakeLists.txt 里给 embark_core 定义；真机在这里给）。
# EMBARK_PLATFORM_HAS_FREERTOS=1 ⇒ embark_config.h 会打开 ETL_TARGET_OS_FREERTOS。
# EMBARK_LOG_SERIALIZE=1 ⇒ 日志加锁（真机是多任务环境：UI 任务 + own task + IDF 任务）。
target_compile_definitions(${COMPONENT_LIB} PUBLIC
        EMBARK_PLATFORM_HAS_FREERTOS=1
        EMBARK_LOG_SERIALIZE=1
        EMBARK_PLATFORM_NAME="esp32"
        EMBARK_VERSION_STRING="0.1.0")

# 真机 UI 后端（ADR 0011 的第 3 步，2026-10-17 接线）：`app/common/ui_backend.h` 里
#   有 EMBARK_AIDOGUI_UI 才把 ui_backend_* 从"三句空实现"切到 AiDoGUI 桥。
# 宿主在 app/CMakeLists.txt:73 打开同一个宏。
# PRIVATE：这两个宏只对 embark 组件自己的 TU 有意义（PUBLIC 会传到最终 elf 目标，
# 那个目标要编一个自动生成的纯 C 文件，别给它塞任何多余的东西）。
#
# EMBARK_AIDOGUI_RES_ROOT = 'S' 盘的根：生成代码把字体路径写死成 S:/fonts/*.bin，
# 真机这份资源就躺在 SD 卡挂载点下（卡上 fonts/ 目录，run19 已就位）。
target_compile_definitions(${COMPONENT_LIB} PRIVATE
        EMBARK_AIDOGUI_UI=1
        "EMBARK_AIDOGUI_RES_ROOT=\"/sdcard\"")

# 固件编译时刻（UTC 秒），2026-10-17 起给板载 RTC 播种用（platform/esp32/src/esp32_time.cpp
# 的 seed_from_build_time）：芯片报 OS=1 时用它把时间写回芯片并清 OS 位。板子没有可信时间源，
# 但"这份固件什么时候编的"是已知的 —— 误差 = 编译到上电那几分钟，日志里会明写这是近似值。
#
# 求值时机是 cmake **配置**阶段（不是每次编译），所以只改代码不重配时这个数会停在上次配置
# 的时刻。对"播种"这个用途来说几分钟偏差无所谓；真要精确对时得等上位机下发时间的通道。
string(TIMESTAMP EMBARK_BUILD_EPOCH "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE EMBARK_BUILD_EPOCH=${EMBARK_BUILD_EPOCH})
