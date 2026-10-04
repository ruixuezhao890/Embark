# 10 · CI 三个 job

Status: resolved
Type: task
Blocked by: 03

## 目标

spec §13 的 CI：三个 job，push 即验。

## 范围

- `.github/workflows/ci.yml`：
  1. `host build + test`（ubuntu，装 SDL2 依赖；编译 + `ctest`）。
  2. `esp32 build`（`espressif/idf:release-v5.4` 容器，`idf.py build`，**只编不烧**）—— 依赖 issue 11。
  3. `clang-format --dry-run`（`.clang-format` 入库；**只在 CI 跑**，本机无 clang-format 不阻塞）。
- 缓存 submodule 与构建目录，控制单次时长。

## 验收

- 三个 job 在 GitHub 上全绿（esp32 job 在 issue 11 落地后变为必需）。
- 失败时能从日志一眼看出是哪一步、哪个文件。
- 本机不装 clang-format 也能正常开发（该 job 只在 CI 卡）。

## 备注

**任何 push 到 GitHub 必须先经用户同意**（用户硬规则）。CI 首次跑通前，push 前先本地跑一遍 host build + ctest。

## Answer

commit `864e46e`：

- `.github/workflows/ci.yml` 三个 job：
  - `host-build-test`：ubuntu-latest，checkout（submodules: recursive）→ apt 装 ninja-build/libsdl2-dev → `cmake -G Ninja -B build` → `cmake --build build` → `ctest --test-dir build --output-on-failure`。
  - `esp32-build`：`espressif/idf:release-v5.4` 容器跑 `idf.py --version` + `idf.py build`；`continue-on-error: true` 占位（platform/esp32 是 issue 11 交付物，落地后去掉占位并转必需）。
  - `format-check`：apt 装 clang-format（ubuntu-24.04 = 18，与本地 pip 18.1.8 同版）→ `clang-format --dry-run --Werror` 扫 `git ls-files` 的 C++ 源（排除 third_party/、.scratch/embark-v1/spikes/、config/lv_conf.h、platform/host/freertos/FreeRTOSConfig.h —— 第三方/存档/官方模板不参与）。
  - 缓存：submodule（third_party，key=hashFiles('.gitmodules')）+ 构建目录（build，key=hashFiles CMakeLists/cmake/源文件），restore-keys 前缀兜底。
- `.clang-format` 原本已入库（Google base + Standard c++17 + ColumnLimit 100 + IndentWidth 2 + PointerAlignment Left + IncludeBlocks Preserve 等），本次只加 `ReflowComments: false`——默认 Google 会把中文注释按 100 列重排，会刷出大量无意义 diff，关掉后 dry-run 才收敛到纯代码差异。
- 一次性格式化对齐（`clang-format -i`，51 个文件、+207/−201 行）：格式化后 `clang-format --dry-run --Werror` 对本仓库 94 个跟踪 C++ 文件（排除集同上）零不符；重建 + ctest 73/495 全绿；三种宿主验收变体 EXIT=0（--frames 60 --switch / --frames 150 --click --switch --own-task / --frames 30 --click）。
- 顺带修复 issue 08 遗留断言 bug：`--switch` 单独跑时（无 --click 先行切到 settings）帧 30/32 的合成点击落在 clock 聚焦屏的 "Settings" 按钮上，`clock brightness` 应为 0 而旧断言写死期望 1 → 现按 `options->click` 分支期望 brightness 1/0，错误消息同步给出期望值（ui_demo.cpp:330-349）。
- 验收状态：本机可做的全部通过（dry-run 0 不符 / 构建绿 / 测试绿 / 宿主验收绿）；GitHub 三 job 全绿需首次 push 后确认（仓库从未 push 过，按用户硬规则等待同意；push 后如有 job 失败再补丁）。
