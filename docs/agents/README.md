# 给 agent 的仓库约定

> 这两份约定原本是分开的两个文件（`domain.md` / `issue-tracker.md`），内容都很短、
> 都是「干活前先读一眼」的性质，因此合成一篇。入口在 [AGENTS.md](../../AGENTS.md)。

---

## 1. Domain docs：读代码前先看术语与决策

本仓是**单上下文**布局：一个 [GLOSSARY.md](../../GLOSSARY.md) + 一个 [docs/adr/](../adr/)，
没有按 `src/<context>/` 再分层的子术语表。

- 探索代码之前，先翻 [GLOSSARY.md](../../GLOSSARY.md)：写 issue、写代码、写文档都用表里的词。
- 动哪块就先读哪块的 ADR：每篇都写了当时的取舍与被否决的方案。
  - 服务层与公开硬件的三条边界：[0019-service-layer-and-public-hardware.md](../adr/0019-service-layer-and-public-hardware.md)
  - 界面代码从哪来：[0011-aidogui-ui-source.md](../adr/0011-aidogui-ui-source.md)
  - App 生命周期与后台：[0015-app-retention-and-refresh.md](../adr/0015-app-retention-and-refresh.md)
- 这些文件缺失就**静默跳过**，不要提示「建议你先建一个」——术语与决策是在真正定下来时才补的。

**输出里出现的领域概念，用 GLOSSARY.md 的词**，别漂到它明确避开的同义词。
要用的概念不在表里，是个信号：要么你在发明项目不用的说法（再想想），
要么真的缺一个词（记下来，等定稿时补）。

**和已有 ADR 冲突时明确说出来**，不要默默改掉：

> _这与 ADR 0010（启动时武装后台）冲突，但值得重开，因为……_

---

## 2. Issue tracker：本地 Markdown

规格与 issue 就在仓库里，随代码提交，是设计决策的一手记录。

### 约定

- 一个 feature 一个目录：`.scratch/<feature-slug>/`
- 规格书是 `.scratch/<feature-slug>/spec.md`
- 工单一人一个文件：`.scratch/<feature-slug>/issues/<NN>-<slug>.md`，
  从 `01` 编号，**不要**合在一个大文件里
- 状态写在文件靠上的 `Status:` 行
- 讨论历史追加到文件底部的 `## Comments` 标题下

### 技能说「publish to the issue tracker」时

在 `.scratch/<feature-slug>/` 下新建文件（目录不存在就建）。

### 技能说「fetch the relevant ticket」时

直接读它给的那个路径的文件。用户通常会直接把路径或编号给你。

### Wayfinding（`/wayfinder` 用）

**map** 是一张总图，每张子票一个文件。

- **Map**：`.scratch/<effort>/map.md`（Notes / Decisions-so-far / Fog）
- **子票**：`.scratch/<effort>/issues/NN-<slug>.md`，从 `01` 编号，正文写问题本身。
  `Type:` 行记类型（`research`/`prototype`/`grilling`/`task`），
  `Status:` 行记 `claimed`/`resolved`
- **阻塞**：靠上的 `Blocked by: NN, NN`。它列的每个文件都 `resolved` 了才算解除
- **前沿**：扫 `.scratch/<effort>/issues/` 里开着的、没被挡的、没认领的；编号小的先做
- **认领**：先写 `Status: claimed` 存盘，再动手
- **结案**：在 `## Answer` 下追加答案，改 `Status: resolved`，
  再把「结论 + 链接」追加到 `map.md` 的 Decisions-so-far
