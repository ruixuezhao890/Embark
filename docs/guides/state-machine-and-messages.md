# 状态机与消息：收消息即切状态

> 目标读者：App 壳已经能跑，需要"收到某条消息就换一种行为"的开发者（人，或 agent）。
> 本文只讲 `etl::fsm` 怎么接进 App 契约。消息本身（类型定义、`publish` / `post`、收件箱信封）
> 见 [concepts/messages-and-background.md](../concepts/messages-and-background.md)；
> 后台策略怎么选见同篇；"状态表达不绑任何范式"的定调见
> [adr/0004-background-tick-and-state-policy.md](../adr/0004-background-tick-and-state-policy.md)。
> 本文的示例代码已实测**编译 + 运行通过**：`g++ -std=c++17 -Wall -Wextra -Wpedantic -fno-exceptions
> -fno-rtti -include config/embark_config.h`（含 `-Iinclude -isystem third_party -isystem build/include`），
> 两个探针 EXIT=0，零告警。

## 0. 先判断：要不要状态机

状态机解决的是"**行为**随输入改变"，不是"**值**随输入改变"。
`app/clock/` 收到 `BrightnessMessage` 只是把亮度百分比存进成员 —— 那是值，不该上状态机。

| 需求 | 用什么 |
| --- | --- |
| 收消息只更新一个值（亮度百分比、传感器读数） | 直接存成员，别上状态机 |
| 收消息要改变**行为 / 模式**（on/off、idle/running/error） | `etl::fsm`（本文） |
| 周期性 / 时序行为（超时、闪烁、轮询节拍） | `etl::fsm` + `onBackgroundTick` 里喂事件（**今天没有 App 声明后台体**；只在看得见时才需要的时序行为改用 `onForegroundTick`，见 [ADR 0015](../adr/0015-app-retention-and-refresh.md)） |
| 进入 / 退出某状态要做副作用（打日志、开定时器、推 UI 变量） | `etl::fsm` 的 `on_enter_state` / `on_exit_state` |
| 只是"上一个 / 下一个"的线性流程 | 一个 `enum` + `switch` 就够，别上状态机 |

框架**不认识**状态机，也不替你管状态 —— 这是 ADR 0004 的定调：给推荐、给例子、不给约束。

## 1. 四个零件

| 零件 | 是什么 | 关键约束 |
| --- | --- | --- |
| `enum StateId : etl::fsm_state_id_t` | 状态的编号 | 必须 `0..N-1` **且按声明顺序**；违反是编译期 `static_assert` |
| `etl::fsm_state<TContext, TDerived, STATE_ID, TMessageTypes...>` | 一个状态；每个状态列出它**关心**的消息类型 | 列几种消息就要写几个 `on_event(const TMsg&)` 重载；**还必须写 `on_event_unknown`** |
| `etl::fsm_state_pack<States...>` | 状态表（值语义，内部 `etl::tuple`） | 状态类必须可默认构造；**数据放 context，不要放状态类** |
| `etl::fsm`（你的 context 继承它） | 状态机本体；同时是 `etl::imessage_router` | 初始状态**恒为 id 0**；`start()` 之前 `receive()` 是空操作 |

`etl::fsm` 的 `is_consumer()` / `is_producer()` 都为真、`accepts()` 恒真
（`third_party/etl/include/etl/fsm.h:573` / `:638` / `:644`），所以它**概念上**可以直接挂
`etl::message_bus`；本框架的总线是私有的，所以走第 3 节的 `onMessage` 转投。

## 2. 最小完整例子

状态机本体：两个状态，各自关心"亮度消息"与"节拍消息"。

```cpp
#include <cstdint>

#include <middleware/etl/fsm.h>

#include <embark/message.h>

using embark::Message;
using embark::MessageT;

// 有基类就不是聚合，必须显式构造（见 reference/pitfalls.md）
struct BrightnessMessage : public MessageT<0x21> {
  constexpr BrightnessMessage(std::uint8_t level_value = 0) noexcept : level(level_value) {}
  std::uint8_t level = 0;  // 0..3 四档
};

struct TickMessage : public MessageT<0x22> {
  constexpr TickMessage() noexcept = default;
};

// 状态 id 必须 0..N-1 且按序（fsm_state_pack 的 static_assert）
enum StateId : etl::fsm_state_id_t { kOff = 0, kOn = 1 };

class LampFsm;  // 前向声明：状态类要用 context，context 要用状态类

class StateOff : public etl::fsm_state<LampFsm, StateOff, kOff, BrightnessMessage, TickMessage> {
 public:
  etl::fsm_state_id_t on_event(const BrightnessMessage& msg);
  etl::fsm_state_id_t on_event(const TickMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

class StateOn : public etl::fsm_state<LampFsm, StateOn, kOn, BrightnessMessage, TickMessage> {
 public:
  etl::fsm_state_id_t on_event(const BrightnessMessage& msg);
  etl::fsm_state_id_t on_event(const TickMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

// context：继承 etl::fsm，状态机自己就是"数据 + 状态"
class LampFsm final : public etl::fsm {
 public:
  LampFsm() noexcept : etl::fsm(0) { set_states(states_); }

  void set_level(std::uint8_t v) noexcept { level_ = v; }
  [[nodiscard]] std::uint8_t level() const noexcept { return level_; }

 private:
  etl::fsm_state_pack<StateOff, StateOn> states_;
  std::uint8_t level_ = 0;
};

// 状态类的成员函数体放在 context 定义之后（类内定义 get_fsm_context() 需要完整类型）
etl::fsm_state_id_t StateOff::on_event(const BrightnessMessage& msg) {
  get_fsm_context().set_level(msg.level);
  return kOn;  // 返回值 = 下一个状态
}
etl::fsm_state_id_t StateOff::on_event(const TickMessage&) { return kOn; }
etl::fsm_state_id_t StateOff::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StateOff::on_enter_state() { return No_State_Change; }

etl::fsm_state_id_t StateOn::on_event(const BrightnessMessage& msg) {
  get_fsm_context().set_level(msg.level);
  return kOff;
}
etl::fsm_state_id_t StateOn::on_event(const TickMessage&) { return kOff; }
etl::fsm_state_id_t StateOn::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StateOn::on_enter_state() { return No_State_Change; }
```

用法就三句：`fsm_.start()` 一次、`fsm_.receive(msg)` 每条、`fsm_.get_state_id()` 观测。

## 3. 接进 App：三条接线

状态机是 App 的**成员**，App 契约本身不变（`include/embark/app.h:95-119` 的八个钩子照旧）。

```cpp
class LampApp final : public embark::App {
 public:
  // ... name() / title() / settings() / 其余钩子 ...

  void onCreate(Framework& fw) override {
    (void)fw;
    fsm_.start();  // 状态机就位（初始状态 = id 0）
  }

  /// ② 消息：框架投递进来的广播，原样转投给状态机
  void onMessage(const Message& msg) override {
    if (msg.get_message_id() == BrightnessMessage::ID) {
      fsm_.receive(msg);
      // 需要"消息即行为"的 App，行为全在 on_event 里
    }
  }

  /// ③ 节拍：tick 策略下周期性喂一个无参事件（状态机不读 now_ms）
  void onBackgroundTick(std::uint32_t now_ms) override {
    (void)now_ms;
    fsm_.receive(TickMessage{});
  }

 private:
  LampFsm fsm_;
};
```

### 3.1 为什么不能直接 `fw.bus().subscribe(fsm_)`

`Framework::bus()` 返回 **`const Bus&`**（`include/embark/framework.h:131`），订阅权在框架手里：
装配期由框架给每个 App 挂一个常驻适配器（`src/embark/framework.cpp:44-54`），
适配器把**所有**广播都投给该 App 的 `onMessage`（`include/embark/framework.h:253-271`，`accepts()` 恒真）。
所以 App 侧唯一入口就是 `onMessage`，转投一行即可。

> 这条与 `.scratch/embark-v1/spec.md` 第 312 行"可直接挂 `message_bus`"的说法有落差：
> ETL 那一层成立（`fsm` 是 `imessage_router`），但 Embark 把总线包成了私有 + `const` 暴露，
> 实际路径是 `onMessage` 转投。**不是 ADR 冲突**，只是 spec 的一句设计意图没落到 API 上。

## 4. 运行语义（`third_party/etl/include/etl/fsm.h` 逐条核过）

- `start()`：`p_state = state_list[0]`（`:510`）—— **初始状态永远是 id 0**，`start()` 不接受参数指定。
- `start()` 会调初始状态的 `on_enter_state()`；**若它返回一个 id，就链式跳过去**（`:518-526`）。
  这条常被用来做"开机自检失败就进错误态"。
- 一次 `receive()` 的固定顺序（`:680-691`）：
  当前状态 `process_event` → 命中 `on_event` → 拿到新 id → **`on_exit_state()`（旧）→ 换指针 → `on_enter_state()`（新）**；
  `on_enter_state()` 再返回 id 就继续链式跳。
- 返回值四种语义：
  - 具体 id → 切过去；
  - `No_State_Change` → 保持原状态；
  - `Self_Transition` → **原地重进**（`on_exit` + `on_enter` 同一个状态，`:659-673`）；
  - **返回自己当前的 id → 什么都不做**（`have_changed_state` 判定为假，`:652-656`）。想重进必须用 `Self_Transition`。
- 没被列进 `TMessageTypes` 的消息走 `on_event_unknown`；`Pass_To_Parent` 是 HFSM 用的，平面状态机用不到。
- 消息 id 连续时按 `id - Message_Id_Start` 直接索引，不连续时二分（`:877-907`）—— 两种都对，不用刻意凑连续。
- 层级状态机（`etl::hfsm`）本文不涉及。

## 5. 常见坑

1. **必须写 `on_event_unknown`** —— 即使你的消息总能命中，`fsm.h:815` 也会实例化它。
   漏了就是编译错：`class X has no member named 'on_event_unknown'`。
2. **每个列进 `TMessageTypes` 的类型都要有对应的 `on_event` 重载** —— 缺一个编不过。
3. **状态 id 不能跳号** —— `fsm_state_pack` 的 `static_assert("State IDs must be 0..N-1 and in order")`。
   用 `set_states(ifsm_state**, size)` 那条路则只有运行期断言，而运行期断言是**静默**的（见第 5 条）。
4. **数据放 context，别放状态类** —— 状态在 `fsm_state_pack` 里默认构造一次，
   是跨切换共享的实例；放进状态类的成员不会随状态切换重置。
5. **`start()` 之前 `receive()` 是静默空操作** —— 走 `ETL_ASSERT_FAIL(fsm_not_started)`（`fsm.h:546`），
   而本仓库 `ETL_LOG_ERRORS 1`（`config/embark_config.h:64-66`）下断言**只回调、不中断**，
   且仓库目前没有注册任何错误回调 ⇒ 什么都不打印、也不会停机。别指望它提醒你。
6. **`onMessage` 会收到自己 `publish` 的消息** —— 总线是广播，发送方的适配器也在订阅表里
   （`src/embark/bus.cpp:55-75`）。转投给状态机前先按消息 id 过滤，否则自回环。
7. **`etl::fsm` 的构造参数是 router id** —— 只有真把它挂到 `etl::message_bus` 上时才必须唯一；
   走 `onMessage` 转投时给 `0` 即可。
8. **别在 `on_event` / `on_enter_state` 里做耗时活** —— `receive()` 在调用它的那条路径上同步跑；
   从 `onBackgroundTick` 喂事件时，它就在唯一 UI 任务里（`tick` 策略，见 messages-and-background.md）。**今天全仓没有 App 声明后台体**，所以这份样板读作「将来真需要后台时怎么做」；纯前台时序改用 `onForegroundTick`（[ADR 0015](../adr/0015-app-retention-and-refresh.md)）。

## 6. 验收清单

- [ ] `cmake --build build` 过 —— 状态 id 顺序、`on_event` 覆盖、`on_event_unknown` 都在编译期查完
- [ ] `ctest --test-dir build --output-on-failure` 过
- [ ] 想观测状态：给 App 加一个 `state()` getter（`fsm_.get_state_id()`），
      照 `app/clock/` 的 `ticks()` / `brightness()` 那样暴露给宿主 CLI
- [ ] 日志里能看到 `on_enter_state` / `on_exit_state` 打的点，确认切换顺序符合预期

## 7. 与 `etl::state_chart` 的关系

v1 早期 `app/clock/` 用 `etl::state_chart` 示范过（ADR 0004「落地与验证」记着那次 issue 08）；
现在仓库里没有 `state_chart` 的实例，`app/clock/` 也不再持状态机。

| | `etl::state_chart` | `etl::fsm`（本文） |
| --- | --- | --- |
| 转移表 | **数据**：两个静态数组 + 运行期线性查找 | **代码**：每个状态一个类 |
| 消息接入 | 事件是自定的 `event_id_t`，要自己从消息翻译 | 直接吃 `etl::imessage`，与总线同源 |
| 适合 | 状态多、转移稀疏，想一眼看完转移关系 | "收消息即切状态"，每个状态有自己的消息集合与副作用 |

ADR 0004 的"状态表达不绑任何范式"仍然成立 —— 框架既不内置状态机，也不强制选型；
本文给的是推荐路线，不是约束。
