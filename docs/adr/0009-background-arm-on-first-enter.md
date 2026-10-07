# App 第一次进过前台时才武装后台策略

后台策略的"开跑"从 boot 挪到 **App 第一次进过前台**：boot 只**注册** `tick` 策略的定时器（`register_timer`，不 `start`）、只**校验** `own_task` 的数量上限（超 `max_own_tasks` 即装配失败、不进循环）；默认前台（注册表第 0 个）在 boot 里已经进过前台，所以 boot 当场武装它，其余 App 等第一次被 `request_switch` 切进前台、`onEnter` 返回后的同一帧武装（`Framework::apply_pending_switch` 里调 `arm_background(foreground_)`）。`arm_background(id)` 幂等，用 `armed_[id]` 记账：`tick` 策略此刻才 `start` 已注册的定时器（周期从这一刻起算），`own_task` 策略此刻才经 `ITaskSpawner` 创建任务，`suspend` 与 `tick + period_ms == 0`（没有后台体）只记一位账。

这样定的理由：用户看到的是"我没打开它，它自己在后台跑"——最初的触发就是 clock（当时全仓唯一非 `suspend` 的 App）在 boot 后 606 ms 打出"clock 后台节拍：第 0 拍（约 0 s）"。框架能约束的只有"后台从何时开始跑"，约束不了"后台何时不跑"：后台一旦开跑就与前后台无关（`tick` 由唯一 UI 任务的每帧 `timers_.tick(1)` 驱动，`own_task` 是平台上的常驻任务），所以能给出的、语义自洽的边界就是**武装时机**。"进过一次前台 = 一直活着"是单一判据，App 不需要为"已进过前台但还没武装"再写一套降级逻辑；武装点放在 `onEnter` 之后而不是 `onPause` 之后，也让第一次进前台的那一屏就能立刻看到后台在工作（clock 屏第一次进去计数就在跳）。

代价与口径：`armed_` 是 RAM 位、每次开机清零，所以语义是"**每次开机之后被打开过一次才跑**"，不是"历史上被打开过"；真机上由持久化状态驱动的 App（例如闹钟要"上电即跑"）将来需要一个显式的 `AppSettings` 字段（eager）才能表达，v1 不加（YAGNI）——**后续（2026-10-07，ADR 0010）已把这个字段加上**：`AppSettings::arm` / `ArmPolicy::at_boot`。武装失败不回滚切换：`suspend`/`tick` 的武装在装配期已经确定不会失败（定时器容量错误在 boot 就暴露），运行期只有 `own_task` 会失败（`no_space` / `busy` / `unsupported`），此时记 `ELOG_ERROR` 与 `arm_failures()` 计数，切换照常完成（boot 路径直接返回失败、不进循环，保持"装配期错误 fail-fast"）。

## Considered Options

- **A. 第一次进前台（`onEnter`）就武装（选定）**：规则单一，第一次进那一屏后台立刻可用；默认前台 boot 即武装，与改动前对启动器/主屏的行为一致。
- **B. 第一次退到后台（`onPause`）才武装**：更贴近"返回 launcher 之后才开始跑"的字面表述，但第一次前台会话里后台是静止的（clock 屏第一次进去计数不动），且框架要多维护一个"已进过前台、未武装"的状态，App 也要容忍这种半启动态。用户 2026-10-06 拍板选 A。
- **C. 保持 boot 武装，另加 eager/开关字段让 App 自己声明**：把判断推给每个 App，默认行为仍是"一上电就跑"，没有回答用户的问题；eager 字段将来真有需求再加（见上）。**（后续：ADR 0010 把这条的"显式声明"路线落了地 —— 字段叫 `ArmPolicy::at_boot`，默认值仍是 `on_first_enter`，所以本 ADR 的默认行为不变。）**
- **D. 让后台"只在前台之外跑"（前台时暂停后台）**：`tick` 还好说（可以跳过），`own_task` 不自洽——它是 `for (;;) { delay(period_ms); onBackgroundTick(now); }` 的常驻平台任务，没有"暂停/恢复"的原语，停下来只能靠删任务重建，成本高且会让 App 的后台状态机断在半路。"后台"描述的是**职责**（不渲染界面、跑轻量逻辑），不是"只在前台之外运行"。

## 落地与验证

- issue 23：武装时机落地（`src/embark/framework.cpp` 的 `arm_background` / boot / `apply_pending_switch`、`include/embark/framework.h` 的 `background_armed()` / `arm_failures()`）。
- 内核用例：`tests/kernel/test_framework_messaging.cpp`（tick 武装前不跑、武装后按周期跑；`period_ms == 0` 无后台体）、`tests/kernel/test_own_task_lifecycle.cpp`（默认前台 boot 即武装、两个 App 各自在武装时创建）、`tests/kernel/test_system_tour.cpp`（系统用例全程带武装断言）、`tests/kernel/test_zero_alloc.cpp`（武装路径零分配）。
- 宿主验收：`platform/host/ui_tour.cpp` 增加"进 clock 之前它的后台没跑""clock 在第一次进前台时就已武装""武装失败计数 = 0"三项自检（清单共 17 项）。
- 交叉阅读：ADR 0004（后台节拍的实现）、ADR 0005（静态槽位与任务池）、`docs/concepts/app-lifecycle.md` §4.1、`docs/concepts/messages-and-background.md`「武装时机」、`GLOSSARY.md`（Arm / Background app / Background tick）。
- 后续（2026-10-07，ADR 0010）："闹钟这类上电即跑"落成显式字段 `ArmPolicy::at_boot`（boot 第 9 步先武装 at_boot 轮、再武装默认前台）；本 ADR 定的默认（`on_first_enter`）不变。
