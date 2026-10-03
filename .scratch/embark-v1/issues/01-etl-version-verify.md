# 01 · ETL 版本复核（第 0 步）

Status: resolved
Type: research
Blocked by: —

## 目标

定下最终要锁的 ETL 版本（本机 20.40.0 vs 上游 20.49.0），消掉 spec §16.3 的版本风险。**这是第一个要做的 issue**。

## 范围

- 拿到上游 `ETLCPP/etl` **20.49.0** 的源码（本机没有；用临时目录浅克隆或 submodule fetch，**不改仓库依赖指针**）。
- 编一个最小 TU，逐个 include：`expected.h`、`optional.h`、`variant.h`、`message_bus.h`、`message_router.h`、`message_packet.h`、`queue.h`、`circular_buffer.h`、`callback_timer.h`、`fsm.h`、`pool.h`、`version.h`，用 C++17 + `-fno-exceptions -fno-rtti` 编过。
- 按 spec §16.3 的 **12 条复核清单**逐条核，每条给「是 / 否 + `文件:行号`」，不许推断。
- 复核 spec §16.2 的宏前置条件是否变化（尤其定时器强制宏、`ETL_LOG_ERRORS`、`ETL_TARGET_OS_FREERTOS`、`ETL_CHECK_PUSH_POP`）。

## 验收

- 本 issue 追加 `## Answer`：最终建议锁哪个版本 + 12 条逐条结果 + 若换 20.49.0 需要对 spec §7/§16 做的具体修订清单。
- 若结论是"仍锁 20.40.0"，也要给出理由（例如上游在 20.41–20.49 引入了我们不需要的破坏性变更）。

## 备注

不动依赖指针、不改 spec 正文；结论先落本 issue，再由用户决定是否改 spec。

## Answer

**结论（建议，已被采纳）：把依赖从 20.40.0 升到 20.49.0 —— 12 条复核全部无回归，且拿到两处对我们直接有用的东西。**（用户 2026-10-03 拍板升级，执行结果见文末第 7 节。）
**本 issue 的核实动作没有改过依赖指针**（只读文件 + 在 `%TEMP%` 里编探针），仓库现在仍锁 20.40.0 = `c882a9c5004d546bb23bd0f758ae40cd57d1b063`；升版等用户点头，要换的清单见文末。

> 历史更正（code-review 指出）：中间提交 `b47dbce` 曾把 submodule 指针记成 etl `4707b6b1`（= 20.49.0-7，`git submodule add` 当时的克隆 HEAD）与 doctest v2.5.0，`ccd1465` 已改回 20.40.0 / v2.4.11。**以 `ccd1465` 及之后的提交为准**，`b47dbce` 里的依赖指针不准。

### 1. 编译验证（12 个头 + Embark 的宏，两个版本都跑）

克隆：`%TEMP%\etl-20.49.0`（浅克隆，未改仓库）。探针 `%TEMP%\embark-etl-probe\probe.cpp` 逐个 include 12 个头，并真实使用 `expected` / `optional` / `variant` / `circular_buffer` / `queue` / `vector` / `message_bus`+Router(`receive`) / `message_packet` / `callback_timer<4>` / `pool<int,4>`：

```
g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic \
    -DETL_NO_STL=1 -DETL_CHECK_PUSH_POP=1 -DETL_LOG_ERRORS=1 \
    -DETL_CALLBACK_TIMER_USE_ATOMIC_LOCK=1 -DETL_MESSAGE_TIMER_USE_ATOMIC_LOCK=1 \
    -I <etl/include> -fsyntax-only probe.cpp
```

- **20.40.0：exit 0（0 error / 0 warning）**
- **20.49.0：exit 0（0 error；唯一 warning 是探针自己没用 `var`）**

**顺带挖出一个必须改的坑（已改）**：把 `ETL_TARGET_OS_FREERTOS` 无条件定义时，`<etl/callback_timer.h>` → `<etl/timer.h>` → `<etl/atomic.h>` → `<etl/atomic/atomic_gcc_sync.h>` → `<etl/mutex.h>` → `mutex_freertos.h` → 需要 `FreeRTOS.h`：
- 20.40.0 报 `fatal error: FreeRTOS.h: No such file or directory`；
- 20.49.0 在 `mutex/mutex_freertos.h:45` 给了友好 `#error "FreeRTOS.h and semphr.h not found…"` 之后同样炸。

也就是说这个宏的影响面不止"选互斥实现"，连 `callback_timer` 都编不过。已改成按平台能力开关：`config/embark_config.h` 读 `EMBARK_PLATFORM_HAS_FREERTOS`（顶层 CMake 传，宿主现在 0；FreeRTOS Windows port 落地见 02/06，真机见 11），只有为 1 时才 `#define ETL_TARGET_OS_FREERTOS 1`。改完 `cmake -G Ninja -B build` + `cmake --build build` + `ctest` 全绿（1/1 passed）。

### 2. §16.3 的 12 条逐条复核（B = 20.49.0，行号均 B 文件实测）

| # | 项 | 结果 | 证据 |
| --- | --- | --- | --- |
| ① | `expected` 新增 `and_then`/`or_else`/`transform` | **是（纯新增）** | `expected.h`：`transform` :1016/:1022/:1028/:1034、`and_then` :1040+、`or_else` :1064+、`transform_error` :1497-1515；A 的 expected.h 里这三个名字命中 **0 次** |
| ② | `expected::value()` 是否仍无断言 | **仍无断言** | :693/:701/:725 四个重载都是裸 `return etl::get<Value_Type>(storage);`；断言只在 `operator->`（:924/:934 `ETL_ASSERT_OR_RETURN_VALUE`）与 `operator*`（:944/:954 `ETL_ASSERT`）→ §16.1 结论（`embark::Result` 自己断言）不变 |
| ③ | 是否新增 `bitmap` | **否** | 全库无 `bitmap.h`、无该标识符 |
| ④ | 是否新增 `interrupt_guard` | **否** | 全库 0 命中 → §16.5「自写」不变 |
| ⑤ | mutex/profiles 是否新增 ESP-IDF/FreeRTOS 后端并自带 `ETL_TARGET_OS_FREERTOS` | **部分 / 否** | 新增的是 **ThreadX**（`mutex/mutex_threadx.h` + `mutex.h:38-40` 分支）；`profiles/` 40 个文件里**没有** freertos/espidf；`ETL_TARGET_OS_FREERTOS` 全库仍只有 `mutex.h:37` 一处消费、**无任何文件定义它** → 仍必须我们自己定义。**意外收获**：`mutex_freertos.h` 改用 `__has_include`，同时认标准布局与 **ESP-IDF 布局**（`<freertos/FreeRTOS.h>`+`<freertos/semphr.h>`，:36-48）→ 真机接 IDF 时互斥后端开箱可用。另：std 分支多了一个条件 `ETL_USING_STD_MUTEX`（`mutex.h:41`） |
| ⑥ | `task` 接口是否变化 | **未变** | 仅格式差异；`task_priority_t` :53、`class task` :58、`virtual uint32_t task_request_work() const = 0` :80、`virtual void task_process_work() = 0` :85 原样 |
| ⑦ | `queue::push` 是否仍返回 void、`ETL_CHECK_PUSH_POP` 是否仍在 | **void 仍在；宏仍在但机制被重构（要记账）** | `void push(const_reference)` :318 / `void push(rvalue_reference)` :333；`emplace` 仍返回 `reference` :351。**`ETL_CHECK_PUSH_POP` 在 A 里散落 12 个文件 82 处，在 B 里只剩 `error_handler.h:537` 一处**：那里定义 `ETL_ASSERT_CHECK_PUSH_POP(b,e)` / `ETL_ASSERT_CHECK_PUSH_POP_OR_RETURN(b,e)`（宏未定义时两者都空展开），容器改调它们（`queue.h:320/:335` push 用 `_OR_RETURN` → **满时提前返回、不写入也不断言**；`emplace` 用 `ETL_ASSERT_CHECK_PUSH_POP` :353+）→ **我们照旧定义 `ETL_CHECK_PUSH_POP=1`**，但我们的薄壳要显式判 `full()` 做丢最旧+计数（§7 已这么定）。新发现：B 另有 `ETL_CHECK_INDEX_OPERATOR` / `ETL_CHECK_EXTRA` 两个检查族（`error_handler.h:551/:563`），debug 下可选打开当前台护栏 |
| ⑧ | `message_bus` 是否新增 `publish` | **否** | 仍只有 `bool subscribe(etl::imessage_router&)` :89，内部 `router_list.full()` 断言 + `upper_bound`+`insert`（与 A 同构）→ §16.5「`embark::Bus::publish()` 自写」不变 |
| ⑨ | `delegate` 存储是否变化 | **签名变了、尺寸没变** | A：`using stub_type = TReturn(*)(void* object, TParams...)`；B：`using object_ptr = void*` :125、`using stub_type = TReturn (*)(const invocation_element&, TArgs...)` :612，`invocation_element` 仍是 `object_ptr object` + `stub_type stub` 两个机器字（:706/:711），另加 `function_ptr`/`ptr_type` 构造。我们不直接用 stub 签名 → 无影响 |
| ⑩ | `pool`/`ipool` free-list 是否又被改 | **未破坏，有新增** | `pool.h:53` 签名未变；`ipool.h` 关键接口仍在（`release_all()` :470、`max_item_size()` :499、`virtual ~ipool()` :676），**新增** `bool is_in_free_list(const char*) const` :140 与 `ipool_iterator` :158；`variant_pool.h` 由 470 行重写为 150 行、改为 `class variant_pool : public etl::generic_pool<etl::largest<Ts...>::size, …, MAX_SIZE_>`（:45）→ 语义等价 |
| ⑪ | `ETL_VERSION_VALUE` 编码 | **未变** | `version.h:62` `((ETL_VERSION_MAJOR * 10000) + (ETL_VERSION_MINOR * 100) + ETL_VERSION_PATCH)`；另新增 `ETL_VERSION_U16/U32/W` 宏与 `version_major/minor/patch` 常量。我们的 `static_assert(… == 20 && … == 40)` 换版时必须同步改（设计如此） |
| ⑫ | `expected<void,E>` API 增删 | **在，且同步增强** | `class expected<void, TError>` :1200，含 `transform` :1425-1445、`and_then` :1449+、`transform_error` :1497-1515 |

### 3. §16.2 宏前置条件复核（逐条重看）

- `ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK` / `..._INTERRUPT_LOCK` 的强制 `#error` 段**逐行相同**（A/B 都在 :50-58）；`message_timer.h` 同比（B :51-59）→ 不变。
- `error_handler.h`：`#if defined(ETL_LOG_ERRORS) || defined(ETL_IN_UNIT_TEST)` 仍在 :46，`ETL_USE_ASSERT_FUNCTION` 仍在 :313 → 「定义 `ETL_LOG_ERRORS`、不要定义 `ETL_USE_ASSERT_FUNCTION`」不变。
- `platform.h`：`ETL_NO_STL` → `ETL_USING_STL 0`（B :97-98）；`ETL_THROW_EXCEPTIONS`（B :287）→ 不变，`-fno-exceptions` 下无需额外定义。
- `ETL_TARGET_OS_FREERTOS`：见上（⑤ + 第 1 节）。

### 4. 这次顺带核到的三处结构变化（写进 §16 的修订清单）

1. `message_packet.h` 从 **5130 行重构成 488 行**：A 是按消息类型数展开的十几个特化，B 是单一模板 `class message_packet`（:49，仍 `IsIMessage || IsInMessageList` 约束，`get()` :190/:196）→ API 兼容（探针里 `etl::message_packet<Ping> packet(ping);` 两版都编过）。§16.1「16 个特化」那句要改。
2. `message_router.h` 大改（`on_receive_unknown` 出现次数 34 → 4），但未匹配消息**仍**转调 `on_receive_unknown()`（B :521/:556）→ §7「必须覆写它做 WARN + 计数」不变。
3. `fsm.h` 大改，`class fsm : public etl::imessage_router` 仍在（B :438，含 `using imessage_router::receive` :443）→ §16.4「App 可直接挂总线」成立。`circular_buffer` 仍原生丢最旧（B :953/:977）。`delegate_observer.h` 改名 `delegate_observable.h`（类名 `delegate_observable` 不变）——我们不使用。另外 ETL 的 `basic_string_stream` 在 A/B 都没有"万能 `operator<<` 模板"（B 全是具体 friend 重载 :128-176）——§15 里那条风险说的是 elog 自己的 `basic_string_stream`，两者别混。

### 5. 建议升版的理由（都基于上面的实测）

- **12 条全过、零回归**：我们依赖的每一项（定容容器、expected 语义、message 设施、环形缓冲丢最旧、定时器强制宏、日志/断言宏、mutex 分派）在 20.49.0 里都还在且行为一致。
- `expected` 的 `and_then`/`or_else`/`transform` 直接利好 §9 —— 可失败链路可以少写一堆 `if (!r) return ...`。
- `mutex_freertos.h` 认 ESP-IDF 布局 → **issue 11 少一个自写后端**（真机上 FreeRTOS 头在 `freertos/` 下）。

### 6. 要换版的话，具体要改的清单（你点头后我一次做完）

1. `git submodule`：`third_party/etl` 指针从 `c882a9c5…`（20.40.0）换到 `7d604f2e4f7fa79ff49bf675c089656943f9171b`（tag 20.49.0）。
2. `config/embark_config.h`：`static_assert` 的 `ETL_VERSION_MINOR == 40` → `== 49`（附文末原句"换版本请重跑 §16 复核"）。
3. spec 修订（本次不动、待你点头）：
   - §16.1 表格里所有 `文件:行号` 引用按 20.49.0 重标（涉及 expected/queue/circular_buffer/message_bus/message_router/fsm/pool/ipool/variant_pool/mutex/version/callback_timer/error_handler/platform 等约 30 处）；
   - §16.1「message_packet 的 16 个特化」→「20.49.0 重构为单一模板（488 行）」；
   - §16.1「环形缓冲与队列」行补一句 20.49.0 的 `ETL_ASSERT_CHECK_PUSH_POP` 机制；
   - §16.2 表格：`ETL_CHECK_PUSH_POP` 行补「20.49.0 起检查集中到 `error_handler.h:537` 的三个 `ETL_ASSERT_CHECK_*` 宏」，并可选新增一行 `ETL_CHECK_EXTRA` / `ETL_CHECK_INDEX_OPERATOR`（debug 护栏）；
   - §16.2 `ETL_TARGET_OS_FREERTOS` 行补「影响面包含 `callback_timer`，必须按平台能力开关（`EMBARK_PLATFORM_HAS_FREERTOS`）」，并把 20.49.0 的 ESP-IDF 布局支持写进 §8 的"目标后端"行；
   - §16.3 后半段（20.40.0 vs 20.49.0 的复核结论）把它从"待复核"改成本节的实测结果；
   - §12 / §15 的 ETL 行：从「先锁 20.40.0 / 20.49.0 待复核」改成「锁 20.49.0（已按 12 条复核）」。
4. 改完重跑：本 issue 第 1 节的编译命令（对 20.49.0）+ `cmake -G Ninja -B build` + `ctest`。
5. 若你决定**不升**（继续 20.40.0）：上面 1–2 项不做，其余"20.49.0 的优点"（`and_then`/IDF 布局）留到 issue 11 时再评估——代价是那时再复核一次 §16 表。

### 7. 执行结果：已升到 20.49.0（2026-10-03，用户拍板「升到 20.49.0」）

- `third_party/etl` 指针已换到 `7d604f2e4f7fa79ff49bf675c089656943f9171b`（`git -C third_party/etl describe --tags` = `20.49.0`），已 stage。
- `config/embark_config.h`：static_assert 改 `MINOR == 49`，并**新增 `#define ETL_CHECK_INDEX_OPERATOR 1`**（20.49.0 的三个边界检查开关 `ETL_CHECK_PUSH_POP` / `ETL_CHECK_INDEX_OPERATOR` / `ETL_CHECK_EXTRA` 定义在 `error_handler.h:537-565`；v1 开前两个，第三个在注释里说明怎么开）。
- 删掉 `tests/smoke/main.cpp` 里那条硬编码 `ETL_VERSION_MINOR == 40` 的用例：换版本时它必然失败，而它是版本守卫的第二份副本 —— 唯一守卫是 `config/embark_config.h` 的 static_assert（强制包含进每个 TU）。
- spec 已按 20.49.0 重标行号：§7（`message_bus.h:409`、`circular_buffer.h:1198`/`:953`/`:977`、`callback_timer.h:828`/`:54`/`:58`/`:69/:70`、`queue.h:320/:335`、版本风险行改为「已消」）、§12 ETL 行、§15 风险行、§16.1 九行、§16.2（`expected.h:246`、`ETL_HAS_MUTEX`/`ETL_HAS_ATOMIC` 出处、`ETL_CHECK_PUSH_POP` 行重写、`ETL_LOG_ERRORS` 分支行号、`ETL_USE_ASSERT_FUNCTION`/`platform.h:287`、repair 宏 `platform.h:255-263`、message/fsm id 宏、profile 派生链）、§16.3（复核结论 + static_assert 行）、§16.4 第 3 条、§16.5（`mutex.h:72`、`function.h:53/:72`）、§16.6 第 3 条（`task.h:80/:85`）；`third_party/CMakeLists.txt` 注释里的版本号与 include 写法同步更正。
- 复验（全绿）：`cmake -G Ninja -B build` + `cmake --build build`（6/6）+ `ctest` → `1/1 Passed 0.31 sec`；`embark_host.exe` 三行 elog、exit 0；探针 `%TEMP%\embark-etl-probe\probe.cpp` 改为走项目自身配置（`-include config/embark_config.h -I build/include -I third_party/etl/include`）→ exit 0。
