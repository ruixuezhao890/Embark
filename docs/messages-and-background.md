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

`App::settings()` 返回 `AppSettings{策略, 周期, 栈深, 优先级}`：

| 策略 | 语义 | 适合 | 注意 |
| --- | --- | --- | --- |
| `suspend`（默认） | 退后台后完全不跑 | 设置页等纯前台交互 App | 前台钩子正常；后台零开销 |
| `tick` | 每 `period_ms` 进一次 `onBackgroundTick(now_ms)` | 轻量后台逻辑（计时、轮询状态、节流） | 回调在**唯一 UI 任务**里执行：必须轻量、不阻塞；周期向上取整到 UI 循环粒度（宿主 5 ms），`period_ms == 0` 等价 suspend |
| `own_task` | 框架经 `ITaskSpawner` 给你建一个 FreeRTOS 任务，周期跑同一个钩子 | 长阻塞、重计算的活 | 栈深/优先级自配（demo：256 字 / 优先级 4，低于 UI 任务的 5）；回调运行在你的任务里，**不能碰 UI 对象、不能 `publish`**，回 UI 走 `post` 信封 |

```cpp
// tick 例：clock 每 100 ms 跳一次（宿主每 20 拍）
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::tick, 100U, 0U, 0U};
}
// own_task 例：ticker 每 50 ms，256 字栈，优先级 4
AppSettings settings() const override {
  return AppSettings{BackgroundPolicy::own_task, 50U, 256U, 4U};
}
```

常用容器与上限（编译期定死，改在 `config/embark_limits.h`）：`max_apps`、
`max_bus_subscribers`、`max_background_timers`、`message_queue_depth`。

## 演示对照

`app/demo_apps.{h,cpp}` 三个 App 就是最小可读范本：clock（tick + state_chart +
收 `BrightnessMessage`）、settings（suspend + `publish` 亮度 + `request_switch`）、
ticker（own_task + `post` 信封）。