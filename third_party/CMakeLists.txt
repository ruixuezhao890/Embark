# 第三方依赖（spec §12）：全部用 git submodule 引入并锁死版本。
#
#   etl         ETLCPP/etl 20.49.0        —— 定容容器、消息、状态机等（spec §16）
#   efmt-elog   ruixuezhao890/efmt-elog   —— 格式化与日志（上游无版本号，锁 commit）
#   doctest     doctest/doctest v2.4.11   —— 宿主单元测试（spec §13）
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

# 挂到公共编译选项上：所有 Embark 目标都能用 <middleware/...>（唯一写法）
target_link_libraries(embark_build_options INTERFACE etl::etl embark_efmt)
