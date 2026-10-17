# 宿主与真机共用的源清单 —— 单一事实来源（issue 20 的结构性收口，2026-10-16）
#
# 谁 include 它：
#   宿主 —— 顶层 CMakeLists.txt 的 include(embark_sources)；src/、platform/common/、
#            app/ 三个子目录从下面的变量取名单。
#   真机 —— platform/esp32/project/components/embark/CMakeLists.txt 与
#            platform/esp32/project/components/embark_font/CMakeLists.txt。
#
# 为什么要有它（issue 20 的两次代价）：
#   1. issue 16 新增 platform/common/nav_shell.cpp 时只改了宿主那份清单：编译期只看得到
#      声明，一路编到最后 Linking CXX executable embark_esp32.elf 才 undefined reference ——
#      宿主 CI 全绿，只有真机链接会把它变成错误；
#   2. 2026-10-16 App 目录化（app/launcher、app/clock、app/settings）后真机组件的
#      INCLUDE_DIRS 没跟上：main.cpp:20 的 #include "clock_app.h" 在编译期直接
#      fatal error: No such file or directory。
#   两类洞的共同点：**宿主那份是对的，真机那份是手抄的**，而真机构建不在 PR 检查里
#   （.github/workflows/ci.yml 的 esp32-build 要推送才跑）。
#
# 收口方式：
#   A. 名单只有这一处文件；
#   B. 真机要编的「共用胶水」由减法推导（EMBARK_COMMON_SOURCES_DEVICE = 共用 − 显式声明的
#      宿主专属），不再手抄 —— 新加一个胶水文件却忘了登记，下面的审计在配置期就报错；
#   C. 真机的 App include 目录由清单里源文件所在目录推导（EMBARK_APP_INCLUDE_DIRS_DEVICE），
#      新 App 自动带上自己的目录；
#   D. 文件末尾是一段配置期审计：登记的路径不存在、归口目录里有 .cpp 没登记、真机 App 子集
#      不是宿主全量的子集、真机 main.cpp 要的 App 头不在 include 目录里 → FATAL_ERROR。
#      "漏项从链接期搬到配置期"就是 issue 20 的验收口径。
#
# 加文件 / 加 App 的流程：改本文件的清单 → 跑一次 cmake 配置（漏了就配不过）。
# 归口目录之外的东西各自由自己的 CMakeLists.txt 管，不在这里：platform/host/、
# platform/esp32/project/（入口与组件）、tests/、app/eez_ui/、app/aidogui_ui/（生成代码）。

if(DEFINED EMBARK_SOURCES_CMAKE_INCLUDED)
  return()
endif()
set(EMBARK_SOURCES_CMAKE_INCLUDED TRUE)

# 本文件在 <仓库根>/cmake/ 下：从自身位置推仓库根，调用方不必再算（各入口里那套
# "上跳 N 级"的路径算术只用于找到本文件）。
get_filename_component(EMBARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# --- 内核（spec §9/§10：零堆、无异常、无 RTTI） -------------------------------
# issue 13 起没有 src/embark/error.cpp：错误码文本由 <embark/error.h> 的 error_text()
# 内联提供，不再单独占一个 TU（删它时真机 configure 直接报 "Cannot find source file"）。
set(EMBARK_KERNEL_SOURCES
        "${EMBARK_ROOT}/src/embark/framework.cpp"
        "${EMBARK_ROOT}/src/embark/bus.cpp"
        "${EMBARK_ROOT}/src/embark/log.cpp"
        "${EMBARK_ROOT}/src/embark/font_ledger.cpp"
        "${EMBARK_ROOT}/src/embark/version.cpp")

# --- 宿主 App 全量（app/<App 名>/ 与 app/common/） -----------------------------
# 真机只编其中一部分（EMBARK_APP_SOURCES_DEVICE，见下）：真机 main.cpp 的 APP_TABLE
# 宿主注册全部 App（六个）与两个薄桥。真机走下面的 DEVICE 子集：2026-10-17 起真机也编了
# AiDoGUI UI 后端（components/embark_aidogui + EMBARK_AIDOGUI_UI），所以 launcher_tiles 与两个
# aidogui 胶水都进了真机。weather/music/album 三个 App 原本在真机外（它们的屏要靠生成代码装），
# 2026-10-17 也放进来了 —— 三个 .cpp 只依赖 framework/log/ui_backend 与本地桩数据，没有平台
# 后端；补齐之后真机主屏的磁贴数与页数才和模拟器一致（此前 2 磁贴 1 页 vs 4 磁贴 2 页）。
# 2026-10-06 目录化后每个 App 各占一个目录，缺一个目录就会让裸头名 #include 失手。
set(EMBARK_APP_SOURCES
        "${EMBARK_ROOT}/app/launcher/launcher_app.cpp"
        "${EMBARK_ROOT}/app/launcher/launcher_tiles.cpp"
        "${EMBARK_ROOT}/app/clock/clock_app.cpp"
        "${EMBARK_ROOT}/app/settings/settings_app.cpp"
        "${EMBARK_ROOT}/app/weather/weather_app.cpp"
        "${EMBARK_ROOT}/app/music/music_app.cpp"
        "${EMBARK_ROOT}/app/album/album_app.cpp"
        "${EMBARK_ROOT}/app/common/eez_ui_bridge.cpp"
        "${EMBARK_ROOT}/app/common/eez_ui_nav.cpp"
        "${EMBARK_ROOT}/app/common/aidogui_ui_bridge.cpp"
        "${EMBARK_ROOT}/app/common/aidogui_ui_nav.cpp"
        # 新 App 登记锚点（tools/scaffold_app.py 在这一行之前插一行；手工加 App 也加在这里）
        )

# --- 真机 App 子集（必须 ⊆ 上面那份） -----------------------------------------
# launcher_tiles.cpp 直接 include aidogui_ui_bridge.h / aidogui_ui_nav.h：真机自 2026-10-17 起
# 也编 AiDoGUI 生成代码（components/embark_aidogui），于是这两个胶水与它一起进真机。
# 打开它们的宏是 components/embark 上的 EMBARK_AIDOGUI_UI=1（与宿主 app/CMakeLists.txt:73 同口径）。
# 2026-10-17 补齐 weather/music/album：真机注册表的六个 App 与宿主同序 ⇒ 主屏磁贴数/页数/
# 天气卡与模拟器一致（用户验收口径「真机画面 = 模拟器画面」）。这三份源码零平台依赖。
set(EMBARK_APP_SOURCES_DEVICE
        "${EMBARK_ROOT}/app/launcher/launcher_app.cpp"
        "${EMBARK_ROOT}/app/launcher/launcher_tiles.cpp"
        "${EMBARK_ROOT}/app/clock/clock_app.cpp"
        "${EMBARK_ROOT}/app/settings/settings_app.cpp"
        "${EMBARK_ROOT}/app/weather/weather_app.cpp"
        "${EMBARK_ROOT}/app/music/music_app.cpp"
        "${EMBARK_ROOT}/app/album/album_app.cpp"
        "${EMBARK_ROOT}/app/common/aidogui_ui_bridge.cpp"
        "${EMBARK_ROOT}/app/common/aidogui_ui_nav.cpp")

# --- 平台共用层（不认平台类型的胶水；宿主把它拆成三个库） ---------------------
# 定容静态池：两侧都编（真机 esp32_lvgl_mem.cpp 用它做 LVGL 静态池）。
set(EMBARK_COMMON_STATIC_POOL_SOURCES
        "${EMBARK_ROOT}/platform/common/static_pool.cpp")

# LVGL 端口 + UI 端口 + 运行时字库服务：两侧都编（宿主 SDL 与真机 esp_lcd + CST328 共用
# 同一份）。
set(EMBARK_COMMON_UI_SOURCES
        "${EMBARK_ROOT}/platform/common/lvgl_port.cpp"
        "${EMBARK_ROOT}/platform/common/lvgl_ui_port.cpp"
        # 运行时字库服务（issue 17）：把 LVGL 的 'F' 盘接到 hal::IFileSystem 并记账。
        # issue 25 起真机也有了 IFileSystem 后端（SD 卡），所以它两侧都编 —— 没插卡时
        # Context::filesystem == nullptr，init() 返回 not_ready，整条路径安静地不生效。
        "${EMBARK_ROOT}/platform/common/font_service.cpp")

# 宿主专属 —— 真机没有对应后端，所以不进真机清单。这个"显式声明"就是收口 B 的减法
# 被减数：归口目录里任何**没写在这里、也不在上面两份**的文件都会让下面的审计报错。
# issue 25 起这份名单是空的：运行时字库服务已经升级成"两侧都编"（见上），真机在没插卡时
# 靠 Context::filesystem == nullptr 让它自然失效，不需要在源清单里区分平台。
set(EMBARK_COMMON_UI_HOST_ONLY_SOURCES)
set(EMBARK_COMMON_AIDOGUI_RES_SOURCES
        # AiDoGUI 资源盘（'S' 盘，ADR 0011）：宿主根 = 仓库里的导出目录（EMBARK_AIDOGUI_RES_ROOT
        # 在宿主 platform/common/CMakeLists.txt 定义）；真机根 = SD 卡挂载点 /sdcard
        # （components/embark 上的同名宏），字体路径就是生成代码写死的 S:/fonts/*.bin。
        "${EMBARK_ROOT}/platform/common/aidogui_res_fs.cpp")

# 真机要编的共用胶水 = 定容池 + LVGL/UI 端口 + AiDoGUI 资源盘（'S' 盘 → /sdcard）。
set(EMBARK_COMMON_SOURCES_DEVICE
        ${EMBARK_COMMON_STATIC_POOL_SOURCES}
        ${EMBARK_COMMON_UI_SOURCES}
        ${EMBARK_COMMON_AIDOGUI_RES_SOURCES})

# --- 中文字库（lv_font_conv 生成物；两侧同一个文件，各成一个独立目标） --------
# 宿主 = platform/common 的 embark_zh_font，真机 = components/embark_font。
# 为什么单独成目标而不是塞进内核/共用层：它是纯 C，而两侧的 C++ 编译口径都强制
# -include config/embark_config.h（C++-only 的 ETL 头），套上去就是一片
# "unknown type name 'namespace'"（issue 20 备注）。清单与再生成见 tools/font/。
set(EMBARK_FONT_SOURCES
        "${EMBARK_ROOT}/assets/fonts/embark_zh_14.c")

# --- 真机后端（platform/esp32/src/；只进真机构建） ----------------------------
set(EMBARK_ESP32_SOURCES
        "${EMBARK_ROOT}/platform/esp32/src/esp32_time.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_log_sink.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_system.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_persistence.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_bus.cpp"
        # 板载 RTC PCF85063（2026-10-17 接上）：走上面那条 I2C_NUM_1，给 Esp32Time::epoch_ms()
        # 提供开机锚点。芯片不在 / 没电池时只让 epoch_ms() 报 unsupported，不影响启动。
        "${EMBARK_ROOT}/platform/esp32/src/esp32_rtc.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_display.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_input.cpp"
        # SD 卡（issue 25）：挂载 + hal::IFileSystem 后端。板级引脚默认没填，
        # 运行时整条路径返回 not_ready（见 esp32_board.h 的 SD 一节）。
        "${EMBARK_ROOT}/platform/esp32/src/esp32_sd.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_filesystem.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_ui_task.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_lvgl_mem.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_bringup.cpp"
        "${EMBARK_ROOT}/platform/esp32/src/esp32_hal.cpp")

# --- 真机 App 的 include 目录（收口 C） ---------------------------------------
# 真机 main.cpp 直接 #include "clock_app.h" 这类**裸头名**，所以每个编进固件的 App 目录
# 都必须在 include 路径上。这里从源文件所在目录推导（2026-10-16 漏的正是这一条）。
set(EMBARK_APP_INCLUDE_DIRS_DEVICE
        "${EMBARK_ROOT}/app"
        "${EMBARK_ROOT}/app/common")
foreach(_embark_app_src IN LISTS EMBARK_APP_SOURCES_DEVICE)
  get_filename_component(_embark_app_dir "${_embark_app_src}" DIRECTORY)
  list(APPEND EMBARK_APP_INCLUDE_DIRS_DEVICE "${_embark_app_dir}")
endforeach()
list(REMOVE_DUPLICATES EMBARK_APP_INCLUDE_DIRS_DEVICE)

# --- 配置期审计（每次 cmake 配置跑一遍；四条规则） ----------------------------
# 与 include 次数无关：GLOBAL 属性保证一次配置只审一次（本文件会被多个组件 include）。
#
# 但**脚本模式要整段跳过**：ESP-IDF 收集组件 REQUIRES 时会用 `cmake -P` 跑
# tools/cmake/scripts/component_get_requirements.cmake，它同样 include 各组件的
# CMakeLists.txt（其中就有本文件）；脚本模式里 file(GLOB ... CONFIGURE_DEPENDS) 直接
# 报错 "CONFIGURE_DEPENDS is invalid for script and find package modes"，整个配置就断在
# 这里（2026-10-17 加 SD 的 REQUIRES 时踩到：REQUIRES 一变，IDF 就重跑这个脚本模式
# 收集 —— 所以 issue 20 收口之后一直没暴露）。审计只在真正的配置阶段跑。
if(CMAKE_SCRIPT_MODE_FILE)
  return()
endif()

# 下面的 file(GLOB) 带 CONFIGURE_DEPENDS：归口目录里新增/删除文件会在下一次构建时
# 自动触发重配 ⇒ 「加了文件忘了登记」当场就断，不必等谁手工重新 cmake。
get_property(_embark_manifest_audited GLOBAL PROPERTY EMBARK_MANIFEST_AUDITED)
if(NOT _embark_manifest_audited)
  set_property(GLOBAL PROPERTY EMBARK_MANIFEST_AUDITED TRUE)

  set(_embark_manifest_lists
          EMBARK_KERNEL_SOURCES
          EMBARK_APP_SOURCES
          EMBARK_APP_SOURCES_DEVICE
          EMBARK_COMMON_STATIC_POOL_SOURCES
          EMBARK_COMMON_UI_SOURCES
          EMBARK_COMMON_UI_HOST_ONLY_SOURCES
          EMBARK_COMMON_AIDOGUI_RES_SOURCES
          EMBARK_FONT_SOURCES
          EMBARK_ESP32_SOURCES)

  # 规则 1：登记的路径必须存在（改名 / 删除后没同步 → 配置期报错，而不是链接期）。
  set(_embark_registered "")
  foreach(_embark_list IN LISTS _embark_manifest_lists)
    foreach(_embark_file IN LISTS ${_embark_list})
      if(NOT EXISTS "${_embark_file}")
        message(FATAL_ERROR
                "embark_sources.cmake：${_embark_list} 里登记的源文件不存在：\n  ${_embark_file}\n"
                "文件改名/删除后要同步 cmake/embark_sources.cmake（issue 20 的配置期检查）。")
      endif()
      list(APPEND _embark_registered "${_embark_file}")
    endforeach()
  endforeach()

  # 规则 2：归口目录里存在的源文件必须都登记过 —— issue 20 要的"漏项变配置期错误"。
  foreach(_embark_pattern
          "${EMBARK_ROOT}/src/embark/*.cpp"
          "${EMBARK_ROOT}/platform/common/*.cpp"
          "${EMBARK_ROOT}/app/*/*.cpp"
          "${EMBARK_ROOT}/platform/esp32/src/*.cpp"
          "${EMBARK_ROOT}/assets/fonts/*.c")
    file(GLOB _embark_found CONFIGURE_DEPENDS "${_embark_pattern}")
    foreach(_embark_file IN LISTS _embark_found)
      if(NOT _embark_file IN_LIST _embark_registered)
        message(FATAL_ERROR
                "embark_sources.cmake：发现没登记的源文件：\n  ${_embark_file}\n"
                "把它加进 cmake/embark_sources.cmake 的对应清单（宿主与真机都从这里取名单）。")
      endif()
    endforeach()
  endforeach()

  # 规则 3：真机 App 子集必须 ⊆ 宿主全量（名字写错 / 漏一份在这里断）。
  foreach(_embark_app_src IN LISTS EMBARK_APP_SOURCES_DEVICE)
    if(NOT _embark_app_src IN_LIST EMBARK_APP_SOURCES)
      message(FATAL_ERROR
              "embark_sources.cmake：EMBARK_APP_SOURCES_DEVICE 里的文件不在 EMBARK_APP_SOURCES 里：\n"
              "  ${_embark_app_src}")
    endif()
  endforeach()

  # 规则 4：真机 main.cpp 里 #include 的 App 头必须能在推导出的 include 目录里找到。
  # 规则 3 查的是"清单里写了什么"，这条查的是"固件真的要用什么" —— 新 App 注册进真机
  # APP_TABLE 却忘了加进 EMBARK_APP_SOURCES_DEVICE 时，断在这里而不是编译期。
  file(STRINGS "${EMBARK_ROOT}/platform/esp32/project/main/main.cpp" _embark_main_lines)
  foreach(_embark_line IN LISTS _embark_main_lines)
    if(_embark_line MATCHES "^[ \t]*#[ \t]*include[ \t]+\"([^\"]+)\"" )
      set(_embark_header "${CMAKE_MATCH_1}")
      file(GLOB _embark_header_hits CONFIGURE_DEPENDS "${EMBARK_ROOT}/app/*/${_embark_header}")
      if(_embark_header_hits)
        set(_embark_header_found FALSE)
        foreach(_embark_dir IN LISTS EMBARK_APP_INCLUDE_DIRS_DEVICE)
          if(EXISTS "${_embark_dir}/${_embark_header}")
            set(_embark_header_found TRUE)
          endif()
        endforeach()
        if(NOT _embark_header_found)
          message(FATAL_ERROR
                  "embark_sources.cmake：真机 main.cpp include 了 ${_embark_header}（在 ${_embark_header_hits} 里），\n"
                  "但它的 App 没登记进 EMBARK_APP_SOURCES_DEVICE —— 推导出的 include 目录里找不到它。")
        endif()
      endif()
    endif()
  endforeach()
endif()
