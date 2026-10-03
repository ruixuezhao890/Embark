# 10 · CI 三个 job

Status: pending
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
