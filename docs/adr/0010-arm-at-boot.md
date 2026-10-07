# 显式声明武装时机：`ArmPolicy::at_boot`

`AppSettings` 增加第 5 个字段 `arm`（`ArmPolicy`：`on_first_enter`【默认】/ `at_boot`），每个 App 自己声明后台**从什么时候开始跑**。`boot()` 第 9 步拆两轮：先按注册顺序把所有 `settings().arm == ArmPolicy::at_boot` 的 App 逐个 `arm_background(index)`，再武装默认前台（注册表第 0 个）—— 所以 `at_boot` 的定时器 / 任务在 boot 里就起来了，哪怕这个 App 永远没被打开过。任一 `at_boot` 轮失败 → boot 返回该 `Error`、不进循环（与默认前台轮同一条 fail-fast 口径）。`on_first_enter` 的 App 走 ADR 0009 的原路（第一次 `onEnter` 之后、同一帧武装）；`arm_background` 幂等 + `armed_` 记账不变，所以 `at_boot` 的 App 后来被切进前台时不会重复武装。

这样定的理由：ADR 0009 定的默认是"进过一次前台才跑"，判据是"用户打开过"—— 对由用户驱动的 App（clock 计数屏）成立，对由持久化状态驱动的 App（闹钟）不成立：没有人会先点开闹钟再等它到点响，它的后台逻辑与"UI 是否加载过"无关。框架不该猜哪种 App 属于哪一类（`suspend` 只表示"后台不跑"，不等于"没有界面"，两种含义混在一起会让"为什么它开机就跑了"不可解释），而 App 自己最清楚自己的后台是否依赖 UI —— 所以给一个显式字段：默认保持安全的 `on_first_enter`，需要 eager 的 App 自己声明。

代价与口径：`at_boot` 的 App 在 boot 期就起跑，等于保留了 ADR 0009 之前"一上电就跑"的行为 —— 如果它的后台逻辑会碰 UI 状态（控件、屏对象），第一次进前台之前必须自己跳过 / 容忍，框架不做保护。`at_boot` 的 `own_task` 计入 boot 第 8 步的容量预检（超 `max_own_tasks` 仍是不进循环的装配失败）；boot 路径上的武装失败是**装配失败**，不是运行期记账（运行期的 `arm_failures_` 只统计 `request_switch` 触发的武装失败）。语义仍是"每次开机"（`armed_` 是 RAM 位），不承诺"装上就永久跑"。

## Considered Options

- **A. `AppSettings.arm` 显式字段（选定）**：默认值 `on_first_enter` 保证 4 字段聚合初始化照旧可用（源码兼容）且"不写就不 eager"是安全默认；声明成本一行；日志里 `arm = embark::ArmPolicy::at_boot` 一眼看出武装时机。
- **B. 框架级"无 UI 服务表"（`EMBARK_SERVICE_TABLE`，不占 8 个 App 槽位）**：适合"目标确实没有界面、也不该算作 App"的常驻服务；但 v1 的每个后台体都还是 App（有自己的钩子、消息、设置），多一套并行注册机制只会给装配顺序多一条歧义路径。真要做无界面服务时再评估。
- **C. 保持 YAGNI 不加字段（ADR 0009 的结论）**：回答不了"闹钟上电即跑"。用户只能自己绕（例如在 `onCreate` 里手动触发一次武装、或假装进过一次前台），把框架的判据抄进每个需要它的 App —— 那正是把判断推给使用者的坏味道。
- **D. 全局编译开关 / 让框架推断（"没有 UI 的 App 就 eager"）**：推断的输入不存在（框架不认识"App 在干什么"），而且会与 `suspend` 的语义打架；一旦猜错，"为什么它开机就在跑"变成无法从声明里读出来的隐式行为。

## 落地与验证

- issue 24：`include/embark/app.h` 的 `ArmPolicy` / `AppSettings::arm`、`src/embark/framework.cpp` boot 第 9 步的两轮武装、`include/embark/framework.h` 文件头契约与"at_boot 例外"。
- 内核用例：`tests/kernel/test_framework_messaging.cpp`（`at_boot` 的 tick App 开机即武装且按周期跑 / `at_boot` 的 own_task App 不必进前台就被创建 / 对照：没声明 `at_boot` 的 own_task 仍等第一次进前台）、`tests/kernel/test_own_task_lifecycle.cpp`（`at_boot` 的 App 先占槽位、非 `at_boot` 的等前台；释放回收照旧）、`tests/kernel/test_zero_alloc.cpp`（`at_boot` 轮零分配）、`tests/kernel/test_format_derive.cpp`（派生打印钉住新字段与枚举名）。
- 宿主验收：`platform/host/ui_tour.cpp` 的 17 项自检 —— 本仓库没有声明 `at_boot` 的 App，所以宿主可观测行为不变（16 通过；唯一红项是 issue 23 记录在案的既有 `clock` 判据，与本 ADR 无关）。
- 交叉阅读：ADR 0009（默认武装时机）、ADR 0004（后台节拍实现）、ADR 0005（静态槽位与任务池）、`docs/concepts/app-lifecycle.md` §4.1 / §4.2、`docs/concepts/messages-and-background.md`「武装时机」、`GLOSSARY.md`（Arm / Background tick）。