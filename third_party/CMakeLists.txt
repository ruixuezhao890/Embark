# 第三方依赖（spec §12）：全部用 git submodule 引入并锁死版本。
#
#   etl         ETLCPP/etl 20.40.0        —— 定容容器、消息、状态机等（spec §16）
#   efmt-elog   ruixuezhao890/efmt-elog   —— 格式化与日志（上游无版本号，锁 commit）
#   doctest     doctest/doctest v2.4.11   —— 宿主单元测试（spec §13）
#
# 依赖相关的编译期宏（ETL_NO_STL、ETL_TARGET_OS_FREERTOS 等）不在这里配，
# 统一放在 config/embark_config.h —— 单一事实来源（spec §16.2）。

# CMake 4 起，第三方工程里 cmake_minimum_required 低于 3.5 会直接报错。
# 这里给第三方一个下限，避免为了拉依赖去改它们的源码。
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ETL 与 doctest 声明的 CMake 下限低于 3.10，会在每次配置时刷两条弃用警告 ——
# 那是上游的历史包袱，不是我们的问题，这里关掉（可以用 -DCMAKE_WARN_DEPRECATED=ON 覆盖回去）。
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "关闭第三方依赖的 CMake 弃用警告" FORCE)

add_subdirectory(etl)      # 上游自带 CMake，导出 etl::etl；include 根 = <repo>/include

# efmt / elog：上游是 header-only 且仓库根没有 CMakeLists，由我们包一层（spec §11）。
# 这个 include 根是上游自己的写法用的（#include <elog/elog.hpp>）；
# 头文件内部引用的 <middleware/...> 由构建目录里的视图满足（见 cmake/embark_middleware.cmake）。
add_library(embark_efmt INTERFACE)
add_library(embark::efmt ALIAS embark_efmt)
target_include_directories(embark_efmt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/efmt-elog")

add_subdirectory(doctest)  # 作为子项目引入时，它自己的测试默认不建

# 挂到公共编译选项上：所有 Embark 目标都能用 <middleware/...> 与 <elog/elog.hpp>
target_link_libraries(embark_build_options INTERFACE etl::etl embark_efmt)
