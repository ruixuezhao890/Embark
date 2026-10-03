# 12 · 文档与新手最短路径

Status: pending
Type: task
Blocked by: 08, 09

## 目标

spec §14.6 的文档验收：新人照着做就能跑起来、加一个 App。

## 范围

- 根 `README.md`：Embark 是什么、目录结构、宿主与 ESP32 两条构建命令、怎么加一个 App。
- `docs/README.md`：索引 + **三条最短路径**（跑起来 → 敲起来 → 改起来）。
- `docs/` 下补：HAL 后端怎么写（宿主/真机各一份骨架）、消息与后台策略怎么用、常见坑（ETL 定容、`lv_conf.h`、宏前置条件）。
- 追加 ADR 记录本轮决定：`docs/adr/0004-background-tick-and-state-policy.md`（后台节拍定 A、状态不绑范式、依赖先锁 20.40.0）。
- 与 `.scratch/embark-v1/spec.md`、`GLOSSARY.md` 交叉链接。

## 验收

- 一名没看过源码的人（或未来的自己）按最短路径能在 Windows 上跑起宿主 demo，并照着加一个只显示一行字的空 App。
- 文档里出现的命令都能直接复制执行（含 `export.ps1`、依赖初始化 submodule 那一步）。

## 备注

按本仓库文档约定：**代码标识符用英文，注释与文档用中文**。
