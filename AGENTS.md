# AGENTS.md

## 本仓硬规则（读代码前先看这一条）

**HAL / 服务接口头里不许出现芯片型号名。** `include/embark/hal/*.h` 里写 `IImu` 可以、
`IQmi8658` 不行；`IAudio` 可以、`IPcm5101` 不行。型号名放实现文件、板级头
（`platform/esp32/src/esp32_board.h`）、注释、文档都行。理由与三层边界见
[docs/adr/0019-service-layer-and-public-hardware.md](docs/adr/0019-service-layer-and-public-hardware.md)。

另：`docs/adr/0002-hal-capability-granularity.md` 已被 0019 取代 —— 它里面「不做设备驱动 /
网络不在 v1 范围」两句**不是现行规则**，别照着写。

## Agent skills

### Issue tracker

Issues live as markdown files under `.scratch/<feature>/` in this repo. See `docs/agents/issue-tracker.md`.

### Domain docs

Single-context layout: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.