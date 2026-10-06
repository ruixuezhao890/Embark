# 第三方依赖（spec §12）：全部用 git submodule 引入并锁死版本。
#
#   etl         ETLCPP/etl 20.49.0        —— 定容容器、消息、状态机等（spec §16）
#   efmt-elog   ruixuezhao890/efmt-elog   —— 格式化与日志（上游无版本号，锁 commit）
#   doctest     doctest/doctest v2.4.11   —— 宿主单元测试（spec §13）
#   lvgl        lvgl/lvgl v8.3.11         —— GUI 库，宿主与目标共用（spec §10、issue 05）
#
# 依赖相关的编译期宏（ETL_NO_STL、ETL_TARGET_OS_FREERTOS 等）不在这里配，
# 统一放在 config/embark_config.h —— 单一事实来源（spec §16.2）。

# CMake 4 起，cmake_minimum_required 低于 3.5 会直接报错：doctest v2.4.11 声明的是 3.0
# （third_party/doctest/CMakeLists.txt:1），少了这行连配置都过不去（实测 CMake 4.0.0-rc3）。
# 作用域只有本目录及以下（add_subdirectory 进来的依赖），不影响 Embark 自己的目标。
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ETL 与 doctest 声明的 CMake 下限低于 3.10，会在每次配置时刷两条弃用警告 ——
# 那是上游的历史包袱，不是我们的问题，这里关掉。没有 FORCE：命令行 -DCMAKE_WARN_DEPRECATED=ON
# 已经有值时这里不会覆盖它。
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "关闭第三方依赖的 CMake 弃用警告")

add_subdirectory(etl)      # 上游自带 CMake，导出 etl::etl；include 根 = <repo>/include

# efmt / elog：上游是 header-only 且仓库根没有 CMakeLists，由我们包一层（spec §11）。
# 上游仓库根【不作为 include 根】：只有 <middleware/elog/...>、<middleware/efmt/...>
# 一种写法（视图见 cmake/embark_middleware.cmake）。上层要是哪天要用 eserde/ecli，
# 再给它们各加一条视图链接，别把仓库根整个摊开。
add_library(embark_efmt INTERFACE)

add_subdirectory(doctest)  # 作为子项目引入时，它自己的测试默认不建

# ---------------------------------------------------------------------------
# LVGL 8.3.11（issue 05）：宿主（SDL2 窗口）与目标（ST7789）共用同一份 GUI 库与配置。
#
# 配置自持在 config/lv_conf.h，不依赖上游 examples/ 里的模板；LVGL 的写法是
# #include "lvgl.h"（LV_LVGL_H_INCLUDE_SIMPLE），所以它不进 middleware/ 视图
# （那个视图只为满足上游写死的 <middleware/...> 路径而存在，见 cmake/embark_middleware.cmake）。
# 没有 FORCE：命令行 -DLV_CONF_PATH=... 已经有值时这里不覆盖它。
# ---------------------------------------------------------------------------
set(LV_CONF_PATH "${PROJECT_SOURCE_DIR}/config/lv_conf.h"
    CACHE PATH "LVGL 配置文件路径（本工程自持：config/lv_conf.h）")

# LVGL 自己的 install() 会触发 CMake 的开发者警告 CMP0177（安装路径归一化）。
# 我们不用 install，但也不想让第三方在配置期刷警告：只对这个子目录把该策略设为 NEW。
set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)

add_subdirectory(lvgl)     # 上游自带 CMake，导出 lvgl::lvgl；include 根 = third_party/lvgl

# LVGL 是第三方全 C 代码且我们不改它：把我们自己的告警标准挡在它外面。
if(MSVC)
  target_compile_options(lvgl PRIVATE /w)
else()
  target_compile_options(lvgl PRIVATE -w)
endif()

# 上游无条件把 examples/ 与 demos/ 也建成静态库；我们的 lv_conf.h 已把
# LV_BUILD_EXAMPLES / LV_USE_DEMO_* 全关（编成空翻译单元），这里让它们不进默认构建。
# 将来要跑官方示例时，去掉这两行属性即可。
if(TARGET lvgl_examples)
  set_target_properties(lvgl_examples lvgl_demos PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

add_library(embark_lvgl INTERFACE)
target_link_libraries(embark_lvgl INTERFACE lvgl::lvgl)

# 挂到公共编译选项上：所有 Embark 目标都能用 <middleware/...>（唯一写法）
target_link_libraries(embark_build_options INTERFACE etl::etl embark_efmt)
