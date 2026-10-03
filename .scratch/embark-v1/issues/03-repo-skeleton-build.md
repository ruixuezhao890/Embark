# 03 · 仓库骨架与构建系统

Status: resolved
Type: task
Blocked by: —

## 目标

把 spec §11 的目录与构建骨架立起来：顶层 CMake、两个 target 的占位、依赖引入、编译开关集中。

## 范围

- 目录：`include/embark/`、`src/`、`platform/host/`、`platform/esp32/`（占位）、`app/`、`tests/`、`config/`、`third_party/`。
- 顶层 `CMakeLists.txt`：C++17，`host` target 起步，`esp32` 占位；内核统一 `-fno-exceptions -fno-rtti`。
- 依赖：`third_party/efmt-elog`（submodule，**锁 commit SHA**）、`third_party/etl`（submodule，**先锁 20.40.0**）；ETL 用上游 `etl::etl` INTERFACE target；efmt/elog 由我们包装 `embark_efmt`（INTERFACE）。
- `middleware/` 视图像 spec §11 在**构建目录**生成（Windows junction）：保证内部写法 `<middleware/efmt/...>` / `<middleware/etl/...>` 可用，**绝不把 `etl/` 目录本身加进 include 路径**。
- `config/embark_config.h`：集中 spec §16.2 的 ETL 宏（定时器二选一、`ETL_LOG_ERRORS`、`ETL_CHECK_PUSH_POP`、`ETL_TARGET_OS_FREERTOS` 等），并加 `static_assert` 锁 ETL 版本（§16.3）。
- 杂项：`.gitignore`（构建目录）、`LICENSE`（MIT）、`.clang-format`（CI 用）、版本 `0.1.0`。

## 验收

- `cmake -G Ninja -B build && ninja -C build` 成功产出一个空壳可执行。
- 最小 smoke 测试能跑（doctest 接入或先 printf，交给 issue 09 补全）。
- `#include <middleware/etl/vector.h>` 与 `#include <middleware/efmt/...>` 都能编过。

## 备注

`.scratch/` 是否随仓库提交：默认按"提交"处理（issue 追踪住在这里），用户有异议再改。

## Answer

骨架已落地并**在本机跑通**（cmake 4.0.0-rc3 / ninja 1.13.2 / g++ 15.1.0，MinGW-w64）：

```
cmake -G Ninja -B build          # 配置通过
cmake --build build              # 6/6 目标编译链接成功
ctest --test-dir build --output-on-failure
                                 # 1/1 Test #1: embark_tests ... Passed
./build/platform/host/embark_host.exe
                                 # Embark 0.1.0（平台 host）宿主骨架启动
                                 # ETL 定容容器：size=2 capacity=4
                                 # middleware 视图 / elog / efmt 链路正常   （exit 0）
```

### 落地的文件（18 个 + `.gitmodules`）

- 构建：顶层 `CMakeLists.txt`、`cmake/embark_middleware.cmake`、`third_party/CMakeLists.txt`、`src/CMakeLists.txt`、`platform/host/CMakeLists.txt`、`platform/esp32/CMakeLists.txt`（占位，不进宿主构建）、`app/CMakeLists.txt`、`tests/CMakeLists.txt`
- 配置：`config/embark_config.h`（§16.2 的宏 + 版本 static_assert）、`config/embark_limits.h`（容量上限初值）
- 代码：`include/embark/version.h`、`src/embark/version.cpp`、`platform/host/main.cpp`（宿主入口，elog INFO 三行）、`tests/smoke/main.cpp`（5 个 TEST_CASE）
- 杂项：`.gitignore`、`.clang-format`、`LICENSE`（MIT，2026）、`README.md`（最小；完整文档归 issue 12）

### 验收对照

| 验收项 | 结果 |
| --- | --- |
| `cmake -G Ninja -B build && ninja -C build` 出空壳可执行 | ✅ `build/platform/host/embark_host.exe` |
| 最小 smoke 测试能跑 | ✅ doctest 5 个用例全过（ctest 1/1） |
| `<middleware/etl/...>` 与 `<middleware/efmt/...>` 都能编过 | ✅ 框架头与测试都走这个写法（`config/embark_config.h` 里 `#include <middleware/etl/version.h>` 就是活证据）；`<middleware/elog/elog.hpp>` 同样走通（code-review 后统一到这一种写法，efmt-elog 仓库根不再作为 include 根） |

### 与范围有出入 / 需要记账的三处

1. **多引入一个 submodule：`third_party/doctest`（v2.4.11，`ae7a13539fb71f270b87eb2e874fbac80bc8dda2`）**。spec §12 的依赖表没列它，但 §13 已定 doctest 为测试框架 → 这里按 §13 落地，建议在 §12 表里补一行。
2. **middleware 视图的实际路径是 `<build>/include/middleware/{etl,efmt,elog}`，include 根给 `<build>/include`**（不是范围里写的 `<build>/middleware`）。原因：内部写法是 `<middleware/etl/version.h>`，若把 `<build>/middleware` 当 include 根，预处理器会去找 `<build>/middleware/middleware/etl/version.h`（第一版构建就是这么挂的，`fatal error: middleware/etl/version.h: No such file or directory`）。多出来的 `elog` 链接是为了 `<middleware/elog/elog.hpp>` 也能写（code-review 之后 efmt-elog 的仓库根不再作为 include 根，`<elog/elog.hpp>` 这种上游写法一律不用）。**spec §11 那句"视图在构建目录里生成"不用改，但建议把 `<build>/include` 这个细节补上**（已补，issue 12 写文档时要引用）。
3. **`ETL_TARGET_OS_FREERTOS` 改成按平台能力开关**（`EMBARK_PLATFORM_HAS_FREERTOS`，顶层 CMake 传，宿主现在 0）：无条件定义会让 `<etl/callback_timer.h>`（经 `atomic.h` → `mutex.h` → `mutex_freertos.h`）去吃 `FreeRTOS.h`。细节与证据见 `issues/01-etl-version-verify.md` 的 Answer 第 1 节。宿主 FreeRTOS Windows port 落地（02/06）后改 1。

### 其它约定（成文在文件注释里，供后续 issue 遵循）

- `embark_build_options`（cxx_std_17 + `-Wall -Wextra -Wpedantic` + `-include config/embark_config.h` + 视图 include 根 + `etl::etl` + `embark_efmt`）与 `embark_kernel_flags`（`-fno-exceptions -fno-rtti`，MSVC `/EHs-c- /GR-`）分开：内核与宿主可执行链后者，**测试可执行文件故意不链**——doctest 的 `REQUIRE`/`SUBCASE` 需要异常（`doctest.h:2818-2852` 否则是 `static_assert(false, "Exceptions are disabled!")`）。"内核无异常"由 `embark_core` / `embark_host` 各自的编译选项保证，`tests/CMakeLists.txt` 里有注释说明。
- 所有 ETL 宏只写在 `config/embark_config.h`（唯一事实来源，强制 `-include` 注入），不碰 ETL 自带的 CMake option；`third_party/CMakeLists.txt` 里关掉了第三方依赖触发的 CMake 弃用警告（ETL/doctest 的 `cmake_minimum_required < 3.10`）。
- 版本 `0.1.0`、`EMBARK_PLATFORM_NAME` 由顶层按平台设（宿主 `"host"`），`embark_core` 用它编译进 `platform_name()`。

### code-review 后的修订（固定点 `ca88ea3...ccd1465`，两轴复审）

复审发现 19 条（Standards 9 / Spec 10），**全部按判定处理完毕并复验**：

- **App 层拿不到内核标志**：`app/CMakeLists.txt` 补链 `embark_kernel_flags`（原先只有 `embark_build_options`）。
- **`embark::apps` 无人链接**：`platform/host` 改为链 `embark::apps`；`EMBARK_PLATFORM_HOST` 定义删掉；`platform/esp32/CMakeLists.txt` 改为 `EMBARK_BUILD_ESP32=ON` 时 `add_subdirectory` 进来（内容仍是带指引的 `FATAL_ERROR`，但不再是永不执行的死文件）。
- **两种 elog 写法并存**：统一为 `<middleware/elog/elog.hpp>`，删掉 `third_party/CMakeLists.txt` 里 efmt-elog 的仓库根 include（`elog/*.hpp` 只 include 标准头 + `<middleware/...>`，删了安全）。
- **命名违规**：`config/embark_limits.h` 三个常量改 `max_apps` / `max_background_timers` / `message_queue_depth`，`main.cpp` 的 `kLoggerName` → `logger_name`。
- **`EMBARK_PLATFORM_HAS_FREERTOS` 静默当 0**（Spec 轴最严重项）：由 CACHE STRING 改成 `option(...)`，注入时包 `$<BOOL:...>`；实测 `-D...=ON` 现在真的变 `-D...=1`。
- **版本号双份**：`include/embark/version.h` 不再硬编码版本宏，改由 `src/CMakeLists.txt` 从 `PROJECT_VERSION` 注入 `EMBARK_VERSION_STRING` / `EMBARK_PLATFORM_NAME`（`src/embark/version.cpp` 兜底 `"0.0.0-unknown"`）；smoke 测试改为「非兜底值 + 两个点 + 只含数字与点」，不再抄常量。
- **重复项清理**：删掉 `embark::build_options` 别名与 `tests/smoke` 里重复的 ETL 版本断言；`README.md` 可执行文件路径改成 `./build/platform/host/embark_host`（Windows `.\build\platform\host\embark_host.exe`）。
- **`CMAKE_POLICY_VERSION_MINIMUM` 保留但去掉 `FORCE`**：实测注释掉会配置失败（`third_party/doctest/CMakeLists.txt:1 Compatibility with CMake < 3.5 has been removed`，doctest v2.4.11 声明 3.0），已写成结论式注释。
- **文件数更正**：本节开头的 19 → **18 个文件 + `.gitmodules`**。
- **复验**：`build/` 全新配置 + 构建 + `ctest` → `1/1 Passed 0.27 sec`；`embark_host.exe` 三行输出、exit 0；`build-on` 验过 `-DEMBARK_PLATFORM_HAS_FREERTOS=ON` 注入正确（临时目录已删）。
