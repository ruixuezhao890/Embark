# 02 · ADR 重写：0002 被取代、0012 加前瞻、新增服务层 ADR

Status: done（2026-10-18）
Blocked by: 01（已解锁，01 已 done）

## 为什么

`docs/adr/0002-hal-capability-granularity.md`（全文 5 行）写死：

> 不做设备驱动，不做传感器抽象，**网络与 OTA 不在 v1 范围内**。
> 代价是上层要用某个具体外设时，需要自己在 App 侧或平台侧实现驱动——这是有意接受的成本，不是遗漏。

这与 Q9=(C)（HAL + 设备服务 + 高层公共资源，含 INetwork）**完全相反**。不改，下一轮 AI 读到的规则是错的。

`docs/adr/0012-weather-data-source.md` 第 4 条自己写着"真数据进来 = 换掉 weather_data.h 的实现……这条 ADR 到那时被取代"。

## 要做的

**① 新增 ADR 0019** `docs/adr/0019-service-layer-and-public-hardware.md`

- 决策：HAL 之上加**服务层**（设备服务 + 公共资源），`hal::Context` 加可空服务槽
- 硬规则：**HAL 里不许出现芯片型号名**（`IImu` 可以、`IQmi8658` 不行）。这是 ADR 0002 原意（"接口被某颗芯片的细节绑死"）的延续，只是把线从"HAL 层"上移到了"服务接口层"
- 明确排除：业务服务（天气服务、音乐服务）不进基建
- 记录被否决的方案：Q9 的 (A)(B)(D) 与 Q10 的 (A)(B)

**② ADR 0002 加取代横幅**

> 已被 [0019](0019-service-layer-and-public-hardware.md) 取代（2026-10-18）：服务层与网络/音频/姿态进入框架范围。本文仅作决策史保留。

**③ ADR 0012 加一行前瞻**（本轮不改 `weather_data.h`，换真数据是 10 号票的下游）

> 网络能力已在 [0019](0019-service-layer-and-public-hardware.md) 立项；天气换真数据的时机见 `.scratch/app-platform/issues/10-network.md`。

**④ `docs/index.md:54` 的 ADR 索引同步**（"框架无网络 ⇒ 编译期桩"这条描述要改）

## 验收

- 三份 ADR 互链正确（0002 → 0019、0012 → 0019、index → 0019）
- 新 ADR 里明确写出"哪条原规则被推翻、为什么"
- 全仓不再出现"框架无网络"这类会让 AI 写错的表述

## 收尾记录（2026-10-18）

**新增** `docs/adr/0019-service-layer-and-public-hardware.md`（130 行）：三段分层（App / 服务层 / HAL 七接口）、`hal::Context` 加可空服务槽、硬规则「HAL 里不许出现芯片型号名」、业务服务不进基建（`IGpio` 进 / `IWeather` 不进）、音频三层表（L1 设备 / L2 流 进基建，L3 业务归用户）+ 音量旋钮硬件事实（PCM5101 控制接口 HW，无寄存器可写 ⇒ 只能软件采样衰减）、`INetwork` 一次做完整但排批次 5、Q9 (A)(B)(D) 与 Q10 (A)(B) 五条否决方案的否决理由、代价三条（多一层、可移植性靠 review 与 AGENTS.md 而非编译器、池与栈的账要重算）、验收四条。**顶部横幅明确写了「代码尚未改」**：HAL 仍 7 接口、`hal::Context` 仍 8 成员，落地拆在 07/08/09/10 号票。

**改**：`docs/adr/0002-hal-capability-granularity.md` 加取代横幅 + 末尾「2026-10-18 更正（现行规则以 0019 为准）」三条（原规则被推翻 / 最有价值的判据保留并上移一层 / 本文不是错的、是范围变了）；`docs/adr/0012-weather-data-source.md` 标题去掉「框架当前没有网络」改成「立项时框架没有网络」+ 前瞻横幅（第 1–3 条仍有效）；`docs/index.md` 0012 摘要改口径 + 新增 0019 行（排在 0018 之后）；`docs/concepts/hal-and-portability.md` 表格第 3 行 + 「2026-10-18 起分三段」段；`docs/concepts/index.md:70-72`「App 自己写驱动」→「App 读服务层接口」；`AGENTS.md` 新增首个章节「本仓硬规则」（型号名禁令 + 「0002 那两句不是现行规则，别照着写」）；`GLOSSARY.md` HAL 条目加「HAL 不是最上层」+ 新增 `Service layer（服务层）` 词条 + Weather stub 改「立项时」；`app/weather/weather_data.h` / `weather_app.h` 注释指向 0019 与 issue 10；`platform/esp32/README.md:135-136` 把音频与 WiFi 从「有意不做」移出、注明已立项。

**验收复核**：grep `框架无网络|框架没有网络|不做设备驱动|网络不在 v1|不在 v1 范围内|没有任何网络`，剩余命中全是合法历史表述（CHANGELOG、AGENTS.md 的提醒、ADR 0002/0012/0019 自身的横幅与引用、concepts 的「HAL 不做设备驱动但服务层做」、GLOSSARY 的「立项时」）。三份 ADR 互链人工核对通过。

**未做（有意）**：本轮不动 `app/weather/weather_data.h` 的数据结构 —— 换真数据是 issue 10 的下游。
