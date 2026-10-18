# AGENTS.md

## 本仓硬规则（读代码前先看这一条）

**HAL / 服务接口头里不许出现芯片型号名。** `include/embark/hal/*.h` 里写 `IImu` 可以、
`IQmi8658` 不行；`IAudio` 可以、`IPcm5101` 不行。型号名放实现文件、板级头
（`platform/esp32/src/esp32_board.h`）、注释、文档都行。理由与三层边界见
[docs/adr/0019-service-layer-and-public-hardware.md](docs/adr/0019-service-layer-and-public-hardware.md)。

另：`docs/adr/0002-hal-capability-granularity.md` 已被 0019 取代 —— 它里面「不做设备驱动 /
网络不在 v1 范围」两句**不是现行规则**，别照着写。

**分工：基建归 agent，App 归用户。** 本仓有两条赛道（[docs/index.md](docs/index.md)「两条赛道」）：
`include/embark/hal/`、服务层、`platform/`、`cmake/`、`tools/`、`docs/` 是**基建**；
`app/<名>/` 里的业务逻辑与 `app/aidogui_ui/screen.aid` 里的界面是**用户自己写**的。
**默认不要替用户写 App 的逻辑与界面** —— 只有用户明确点名某个 App 时才动它。
用户的诉求原话是「AI 帮我把硬件基础设施做好，然后我自己编写 App 的逻辑与界面」，
见 ADR 0019 与 `.scratch/app-platform/spec.md`。

**改了工具或开关，就跑一次 `node tools/check_docs.mjs`。** 它检查文档里的路径 / `embark_host_ui`
开关 / 脚本参数 / 锚注释还对不对得上。这个仓库栽过一次：工具早换了，`quickstart.md` 还在教旧做法 ——
过期的指路比缺文档更害人，因为读者会照着做。

## Agent skills

### Issue tracker

Issues live as markdown files under `.scratch/<feature>/` in this repo. See `docs/agents/issue-tracker.md`.

### Domain docs

Single-context layout: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.