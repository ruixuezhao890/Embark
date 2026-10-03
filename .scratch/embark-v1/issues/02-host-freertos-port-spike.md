# 02 · 宿主 FreeRTOS（Windows port）可用性 spike

Status: pending
Type: prototype
Blocked by: —

## 目标

验证宿主能不能用 FreeRTOS（kernel + Windows port）在 **MinGW-w64 GCC 15.1** 上编过并稳定跑任务 —— 这是 spec §3「两端任务模型一致」的前提。

## 范围

- 从归档工程 `E:\01_Workspace\01_Archives\lvgl_template_laste` 取 FreeRTOS kernel 与 Windows port（**只读，不改归档**）。
- 写一个**独立 spike**（不并入仓库骨架）：建 2 个任务、`vTaskDelay`、串口/stdout 打印心跳、跑够 1000 跳。
- CMake + Ninja + GCC 15.1；记录所需源码清单、CMake 片段、编译/链接坑（`portmacro.h` 的 naked 函数、`win32` 端口对 Win32 API 的依赖、栈对齐、需要链接的库）。

## 验收

- 跑通：给出可直接搬进 `platform/host/` 的源码清单 + CMake 片段 + 一份「踩坑记录」。
- 跑不通：给出「最小 win32 port」的改造方案与预估工作量，并明确替代路径（例如宿主用 `std::thread` 实现同一套 `embark` 任务接口，两端行为差异只落在平台层）。

## 备注

这是风险验证，产出直接决定 issue 06 的实现路径。
