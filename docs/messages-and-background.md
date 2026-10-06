# 消息与后台策略

Embark 里 App 之间**不互相 include、不碰对方符号**（spec §7、issue 08 验收项），
一切协作都走框架的两种消息路径；App 退后台后怎么活，由它自己的后台策略决定。
本文讲"怎么用"，概念与取舍见 `adr/0004-background-tick-and-state-policy.md` 与
[GLOSSARY](../GLOSSARY.md)。

## 消息模型：两条路径

| 路径 | 载体 | 语义 | 什么时候用 |
| --- | --- | --- | --- |
| 总线广播 | `Framework::publish(msg)` / `Bus`（`etl::message_bus`） | 同任务内**同步**：所有已订阅者依次收到 | App 之间、UI 任务内部（前台交互、演示消息） |
| 收件箱信封 | `Framework::post(CrossTaskMessage)` / `MessageQueue` | 跨任务**异步**：单生产者对 UI 任务，UI 任务在循环边界抽干回派 | `own_task` 后台 App 往回发任何东西 |

### 消息类型怎么定义

派生 `embark::MessageT<ID>`，ID 是消息型在进程内的唯一身份：

```cpp
#include <embark/message.h>

// 有基类就不是聚合了，必须显式构造（这和 struct 直觉不同，见 common-pitfalls）
struct BrightnessMessage : public embark::MessageT<0x21> {
  constexpr BrightnessMessage(std::uint8_t level_value = 0) noexcept : level(level_value) {}
  std::uint8_t level = 0;  // 0..3 四档
};
```

ID 分配：`0xFE` 是框架保留的跨任务信封（`CrossTaskMessage`），**不要用**；
demo 用 `0x21`；自己的 App 挑不冲突的值即可，并写进注释。
判断消息身份在接收端：`msg.get_message_id() == BrightnessMessage::ID`
（`MessageT` = `etl::message<Id>`，自带静态 `ID` 与实例 `get_message_id()`）。

### 怎么收消息

App 端**不需要显式订阅**：框架装配阶段给每个 App 挂了一个常驻适配器，
所有广播都会被投递到每个 App 的 `onMessage(const Message&)`。收方自己挑：

```cpp
void MyApp::onMessage(const Message& msg) override {
  if (msg.get_message_id() == BrightnessMessage::ID) {
    const auto& b = static_cast<const BrightnessMessage&>(msg);  // ID 匹配后安全
    level_ = b.level;   // 更新自己的副本
  }
}
```

### 怎么发消息

```cpp
// 同任务内：直接广播（无人订阅会计数 + WARN，不会 blocking）
fw.publish(BrightnessMessage{new_level});
```

own_task App 不能用 `publish`（跨线程！），只能 `post` 信封、回 UI 再进总线：

```cpp
// MyOwnTaskApp::onBackgroundTick（运行在自己的任务里）
fw.post(embark::CrossTaskMessage(id_of(*this), ++seq_));
// 信封回到 UI 任务后按序派发给自己，onMessage 里按 seq 对账
```

`onCreate(Framework& fw)` 里保存 `fw_ = &fw;` 之后用（demo 里叫 `fw_`）。

## 后台策略：三种，每 App 声明一次

`App::settings()` 返回 `AppSettings{策略, 周期, 栈深, 优先级, 武装时机}`（最后一项有默认值，4 字段写法照旧可用）。
注意：**声明 ≠ 一上电就在后台跑** —— 默认在 App **第一次进过前台**那一刻才武装；要"开机就跑"得显式声明 `ArmPolicy::at_boot`（见本节末「武装时机」）：

| 策略 | 语义 | 适合 | 注意 |
| --- | --- | --- | --- |
| `suspend`（默认） | 退后台后完全不跑 | 设置页等纯前台交互 App | 前台钩子正常；后台零开销 |
| `tick` | 每 `period_ms` 进一次 `onBackgroundTick(now_ms)` | 轻量后台逻辑（计时、轮询状态、节流） | 回调在**唯一 UI 任务**里执行：必须轻量、不阻塞；周期向上取整到 UI 循环粒度（宿主 5 ms），`period_ms == 0` 等价 suspend；**武装之后才起跑**（默认"第一次进前台"，`at_boot` 在 boot 第 9 步） |
| `own_task` | 框架经 `ITaskSpawner` 从静态池里给你分一个槽、建一个 FreeRTOS 任务，周期跑同一个钩子 | 长阻塞、重计算的活 | 任务在**武装时**才创建（默认"第一次进前台"、不是 boot；`at_boot` 则在 boot 第 9 步）； 栈深/优先级自配（demo：256 字 / 优先级 4，低于 UI 任务的 5）；栈深口径是 `StackType_t` 字（宿主 1 字 = 8 字节，真机 xtensa 1 字 = 1 字节，见 `docs/common-pitfalls.md`）；回调运行在你的任务里，**不能碰 UI 对象、不能 `publish`**，回 UI 走 `post` 信封 |

```cpp
// tick 例：clock 每 100 ms 跳一次（宿主每 20 拍）
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::tick, 100U, 0U, 0U};
}
// own_task 例：ticker 每 50 ms，256 字（StackType_t 字）栈，优先级 4
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::own_task, 50U, 256U, 4U};
}
```

```cpp
// at_boot 例：闹钟这类"上电就要工作"的 App —— 不等用户点开，boot 第 9 步就武装
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::own_task, 50U, 256U, 4U, ArmPolicy::at_boot};
}
```

### 武装时机：默认第一次进前台才开跑，`at_boot` 开机就开跑（issue 23 / ADR 0009；issue 24 / ADR 0010）

默认情况下框架**不会**在 boot 时就把所有 App 的后台拉起来：boot 只给 `tick` 策略注册定时器（不 `start`）、
只校验 `own_task` 策略的数量（超 `max_own_tasks` = 装配失败）；真正的「开跑」发生在 App
**第一次进过前台**的那一刻（`onEnter` 之后、同一帧）。默认前台（注册表第 0 个）在 boot 里
就进过前台，所以它当场武装；唯一的例外是 `ArmPolicy::at_boot`（下面第二条）。

- 武装一次即长期有效：之后退回后台（`onPause`）后台照跑 —— 不是「只在前台之外跑」
  （`own_task` 在平台上是常驻任务，没有暂停/恢复这回事）。
- 没被打开过的 App，后台**完全不跑**：用户视角「我没开它，它自己在跑」不再成立。
- 观测：`fw.background_armed(id)`（是否已武装）、`fw.arm_failures()`（运行期武装失败累计）。
- `entered_` / `armed_` 都是 RAM 位（每次开机清零）：默认口径的准确说法是「**每次开机后被
  打开过一次才会跑后台**」。
- **要「开机就开跑」就显式声明**（issue 24 / ADR 0010）：`settings()` 第 5 个字段传
  `ArmPolicy::at_boot` —— boot 第 9 步先武装所有 `at_boot` 的 App（按注册顺序），再武装默认
  前台；**任一轮失败 = 装配失败**（不进循环）。代价是这个 App 的前台可能还没加载过，它的
  后台已经在跑，后台逻辑要自己跳过 UI 相关的事（等 `onEnter` / `onForegroundTick` 之后再做）。

```cpp
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::own_task, 50U, 256U, 4U, ArmPolicy::at_boot};
}
```
- 机制细节与理由：[app-lifecycle/README.md](app-lifecycle/README.md) 4.1 / 4.2、[adr/0009](adr/0009-background-arm-on-first-enter.md)（默认时机）、[adr/0010](adr/0010-arm-at-boot.md)（`at_boot`）。

常用容器与上限（编译期定死，改在 `config/embark_limits.h`）：`max_apps`、
`max_bus_subscribers`、`max_background_timers`、`message_queue_depth`、`max_own_tasks`
（同时在跑的 `own_task` 上限，也是静态任务池的槽数）。

## own_task 的生命周期：入口返回 = 结束

`own_task` 不是"建了就常驻"：**入口函数返回 = 任务结束**。框架的 trampoline 把这个槽位标成
`finished` 并让任务 park 住（不再被调度），`Framework::step()` 每帧在回收段把槽位还给静态池 ——
之后同一个 App 可以再创建一次，槽位复用、世代号 +1（"创建 → 跑完 → 回收 → 再创建"能反复走）。

```cpp
// 常驻写法：period_ms > 0，框架按周期反复调 onBackgroundTick
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::own_task, 50U, 256U, 4U};
}

// 一次性写法：period_ms = 0，框架只调一轮 onBackgroundTick，返回后任务就结束
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::own_task, 0U, 256U, 4U};
}

// 运行期再创建一次：只能在 UI 任务里、boot 之后 shutdown 之前调用
const embark::Error err = fw.spawn_own_task(fw.id_of(*this));
// none / not_found / not_ready / unsupported / no_space / busy
```

三条纪律：

- **回收只能由持有者做**，持有者就是唯一 UI 任务（它也是唯一调 `spawn_task` 的人）。任务自己删
  自己会走 FreeRTOS 的"删除中"分支（静态 TCB 要等空闲任务才真正释放），此时复用存储会写出
  致命别名。`ITaskSpawner::release_task` 只给持有者用，任务内部不要碰。
- **池满就是 `no_space`**，不排队、不降级：同时在跑的 `own_task` 上限 = `max_own_tasks`（静态池的
  槽数），超了直接拒绝创建。失败点唯一且确定是刻意的，见 `adr/0005-static-task-slots-and-pool.md`。
- **别在自己的任务里等结果**：要等外部事件（WiFi 连接、超时）就阻塞在自己的任务里，成功或超时后
  `return`，槽位自然回收 —— UI 全程不被阻塞。这正是"运行期创建、完成即回收"的第一个真实用例。

观测：`fw.own_tasks_spawned()` / `fw.own_tasks_released()` 是累计创建/回收计数；任务池自身的
`running() / finished() / free_slots()` 在 `platform/common/pooled_task_spawner.h` 上。系统用例见
`tests/kernel/test_own_task_lifecycle.cpp` 与 `platform/host/ui_tour.cpp` 的第 9 步。

## 演示对照
`app/launcher/`（主屏 + 导航接线）、`app/clock/`（tick 100ms + 状态机 + 收 `BrightnessMessage`）、`app/settings/`
（suspend + `publish` 亮度消息 + `bump_level` 逻辑入口）就是最小可读范本；`app/common/app_messages.h` 定义了
`BrightnessMessage`。own_task 的完整系统用例见 `tests/kernel/test_own_task_lifecycle.cpp`（job 模式的一次性任务在
`platform/host/ui_tour.cpp` 的系统用例里驱动，宿主演示 `ui_demo` 仍按这 3 个 App 注册）。