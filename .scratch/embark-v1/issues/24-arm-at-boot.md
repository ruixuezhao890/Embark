# 24 · 武装时机可声明：ArmPolicy::at_boot 让"一上电就要工作"的 App 开机即武装

Status: resolved
Type: task
Blocked by: —
来源: 用户提问（m00409）—— "像闹钟这种一上电就要开始工作的在这个框架下该怎么办呢？" ADR 0009 定的默认（第一次进过前台才武装）对"由持久化状态驱动、不依赖用户点开"的 App 是错的默认。

## 现象 / 问题

ADR 0009 之后 `armed_` 与 `entered_` 绑死：没被打开过的 App，后台一次都不跑。对"由人打开才有意义"的 App（clock 计数屏）正确，对闹钟不成立 —— 没有人会先点开闹钟再等它到点响，它的后台逻辑必须在开机装配后就跑起来。issue 23 备注里那句"将来再加 eager 字段"在本 issue 落地，名字叫 `ArmPolicy::at_boot`。

## 决策（ADR 0010）

选 **A：`AppSettings` 加显式字段 `arm`（`ArmPolicy`：`on_first_enter`【默认】/ `at_boot`）**。`boot()` 第 9 步拆两轮：先按注册顺序把所有 `settings().arm == ArmPolicy::at_boot` 的 App 逐个 `arm_background(index)`，再武装默认前台（注册表第 0 个）。任一 `at_boot` 轮失败 → boot 返回该 `Error`、不进循环（与默认前台轮同口径）。`on_first_enter` 的 App 行为不变（ADR 0009），`arm_background` 幂等 + `armed_` 记账不变。

弃选：B 框架级"无 UI 服务表"（不占 App 槽位）—— v1 的后台体都还是 App，多一套机制无收益；C 保持 YAGNI 不加字段 —— 回答不了闹钟，用户只能自己绕；D 全局编译开关 / 让框架猜 —— 框架不该猜，且与 `suspend` 语义打架。详见 ADR 0010 的 Considered Options。

## 落地

- `include/embark/app.h`：新增 `ArmPolicy`（`E_FMT_DERIVE_ENUM`，默认 `on_first_enter`）与 `AppSettings::arm`（第 5 字段，有默认值 → 4 字段聚合初始化照旧可用）。
- `src/embark/framework.cpp`：`boot()` 第 9 步改两轮（`:107-125`）：`at_boot` 轮（注册顺序）→ 默认前台轮；`include/embark/framework.h` 文件头补 issue 24 的例外与"boot 里武装失败 = 装配失败"。
- 内核用例：`tests/kernel/test_framework_messaging.cpp`（`at_boot` 的 tick App 开机即武装、跑满周期前 0 拍；`at_boot` 的 own_task App 不必进前台；对照：没声明 `at_boot` 的 own_task 仍等第一次进前台）、`tests/kernel/test_own_task_lifecycle.cpp`（两个 App：`at_boot` 的先建、非 `at_boot` 的等前台；回收照旧）、`tests/kernel/test_zero_alloc.cpp`（`at_boot` 轮零分配）、`tests/kernel/test_format_derive.cpp`（新字段与枚举名的派生打印）。
- 测试基础设施修复：`tests/kernel/test_framework_messaging.cpp` 新增 `unbind_records(registry)` —— `detail::app_instance<T>()` 是每**类型**一个进程级单实例，跨用例复用时上一用例的 `Records` 已析构，boot 触发的 `onCreate` 会 push_back 进已释放的 vector（SIGSEGV，gdb 回溯 `framework.cpp:36 onCreate`）。凡在 boot 前复用共享单实例的用例都要先解绑。
- 文档：`docs/adr/0010-arm-at-boot.md`（新增）、`docs/adr/0009`（后续一行 + option C 注记）、`docs/app-lifecycle/README.md`（§2 装配顺序、新增 §4.2、§5 / §7 索引）、`docs/messages-and-background.md`（「武装时机」）、`docs/new-app-guide.md`（钩子契约表）、`GLOSSARY.md`（Arm / Background tick）、`README.md` / `docs/README.md`（宿主流程叙述）。

## 验收

- `build/tests/embark_tests.exe`：104 用例 / 921 断言全绿（改动前 100 用例 / 887 断言）。
- 宿主 tour：17 项自检 16 通过；`at_boot` 相关的三项（"进 clock 之前它的后台没跑""clock 在第一次进前台时就已武装""武装失败计数 = 0"）保持绿 —— 本仓库没有声明 `at_boot` 的 App，因此宿主可观测行为不变。唯一红项是 issue 23 记下的既有项（工作区 `app/clock/clock_app.cpp:55` 的 `chart_.start()` 被注释，`ticks_` 恒 0），与本 issue 无关。
- 语义边界：`at_boot` 的 `own_task` 计入 boot 第 8 步容量预检（超 `max_own_tasks` 仍 fail-fast）；`at_boot` 只表示"开机就武装"，不保证"后台逻辑不依赖 UI 状态"—— 那由 App 自己容忍。

## 备注

- `armed_` 仍是 RAM 位：语义是"每次开机时按声明武装"，不是"装上就永久跑"。
- **暂缓（2026-10-07，用户 m00655）**：下面 ①② 两件事先不做、留待"之后再做"；动手时各开一个独立 issue（不要塞回本 issue）。
- 未在本 issue 内解决、需要另开 issue 的两件事：① **后台到点抢前台**（own task `post` 信封 → 收件箱 → 总线广播 → 订阅者 `onMessage` → 可 `request_switch`；**不要**让 own task 直接 `request_switch`，跨任务写 `pending_` 无线程安全承诺）；② **深睡 / RTC 唤醒 + 启动原因 + "开机直接进闹钟屏"**（当前 HAL 无此能力，boot 固定注册表第 0 个进前台）。

## Comments

### 2026-10-07 · 用户提问与拍板

- 用户（m00409）："像闹钟这种一上电就要开始工作的在这个框架下该怎么办呢？" → 汇报三件事拆分（① 后台逻辑开机即跑 = 框架需声明；② 闹钟设定值跨重启存活 = App 自己用 `hal.storage`；③ 关机 / 深睡到点响铃 = HAL 能力，当前缺失）+ 方案 A（`AppSettings` 加显式字段）/ B（无 UI 服务表），用户（m00433）选 A："A吧"。
- 讨论中确认：`onCreate(Framework&)` 是唯一拿到 Framework 引用的钩子（`include/embark/app.h:95`），onBackgroundTick 等不收 fw —— 这也是 ① 之外"后台想切前台"必须走消息的原因。