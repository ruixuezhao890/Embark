# 07 · 服务层骨架（Context 加三个可空槽）

Status: done（2026-10-18）
Blocked by: 02（已 done）

## 为什么

Q9=(C) 拍板：基建 = HAL + 设备服务 + 高层公共资源。但今天架构里**没有"服务"这个概念**：

- `include/` 27 个头，grep `services/|Service|INetwork` = 0 命中
- `hal::Context`（`include/embark/hal/context.h:26-35`）只有 8 个成员：`time / storage / log / system / bus / display* / input* / filesystem*`
- 网络、音频、GPIO、IMU **在架构上没有位置**

这一票只做"给服务层安一个名字"，不实现任何服务。它是 08/09/10 的前置。

## 接口形状

`include/embark/hal/context.h` 加三个可空成员，与 `display` / `input` 同款（指针 + 默认 nullptr）：

`IImu* imu = nullptr;`
`IAudio* audio = nullptr;`
`INetwork* network = nullptr;`

**放在 `hal::` 命名空间下，不新开 `embark::services::`。** 理由：`Context` 是唯一的注入点（`Framework::hal()`，`include/embark/framework.h:200`），开第二个命名空间就要开第二个注入面，每个 App 都得拿两份引用。硬规则"接口里不许出现芯片型号名"不因放哪而改变。

## 一个必须纠正的说法

我先前在 `spec.md` 里写过"改 `context.h` 一处，编译器会逐个点名所有既有装配点"。**这是错的**：`Context` 是聚合体，C++17 的聚合初始化允许初始化项少于成员数，缺的按值初始化（指针 → `nullptr`）⇒ **三个装配点都会静默编译通过，编译器一句话不说**。

所以票里不能靠编译器兜底，只能靠**清单 + grep 验收**。三个装配点：

| 文件 | 行 | 现状 |
| --- | --- | --- |
| `platform/esp32/src/esp32_hal.h` | :67 | `context_{time_, storage_, log_, system_, bus_, &display_, &input_, nullptr}` |
| `platform/host/host_context.h` | :58 | `context_{time_, storage_, log_, system_, bus_, nullptr, nullptr, &filesystem_}` |
| `tests/fakes/fakes.h` | :34 | `return hal::Context{time, storage, log, system, bus, &display, &input, nullptr};` |

三处都补齐到 11 项（服务槽本轮全填 `nullptr`，08/09/10 各自接上）。

## 同时要改的术语

`GLOSSARY.md` 两条与 Q9 冲突：

- **:85-87 HAL** 现在写「向 App 暴露芯片能力（显示、输入、时间、持久化、日志后端、系统控制）；**不承担逐外设驱动**」——后半句是 ADR 0002 的原话，Q9 已经推翻。改成「HAL = 芯片能力 + 设备服务 + 高层公共资源的统一注入面；**不承担业务语义**（播放列表、天气数据这类归 App）」。
- **:101-103 HAL Context** 现在写「把**七个**能力接口装配成一个引用聚合体」——数字会变成 8（含 filesystem）→ 11。改成不写死数字：「把本平台提供的能力与服务装配成一个引用聚合体；每个平台一份实例，内核不持全局单例」。

## 新增单测

`tests/hal/test_context.cpp`：

- 全槽填满后逐个 `REQUIRE` 指针相等（防有人把某两个成员写反——`display` / `input` / `filesystem` 都是 `lv_obj*` 之外的裸指针，写反了编译器也看不出来）
- 服务槽缺省 = `nullptr`
- `Framework` 拿到 `Context` 后 `hal().imu == nullptr` 时各 App 不崩（今天没有 App 读它，所以这条是给 08/09/10 打地基）

## 验收

- 三个装配点各 11 项：`tests/fakes/fakes.h` 的 `hal::Context{...}`（唯一一处聚合构造）+ `platform/esp32/src/esp32_hal.h` 与 `platform/host/host_context.h` 的 `context_{...}` 成员初始化式（原文写「grep 恰好 3 处构造表达式」不准：另两处走的是成员初始化）
- `grep -rn "services/\|embark::services" .` = 0 命中（确认没有另开命名空间）
- `cmake --build build` + `ctest --test-dir build --output-on-failure` 通过
- `embark_host_ui --ui --delay 40` 不回归

## 收尾记录（2026-10-18）

**实现与票面的偏差（一处，改回事实）**：票面写三个 `IService*` 槽，实际做的是 `IImu* imu` / `IAudio* audio` / `INetwork* network` —— 类型按 ADR 0019 的能力名取，不搞共用基类。两条理由：① 0019 与 `AGENTS.md` 的硬规则已经把三个名字定死（`IImu` 可以、`IQmi8658` 不行）；② 共用空基类等于把「这三块硬件有共同接口」这个假事实写进类型系统，真实接口一落地就得把 Context 槽类型与三个装配点全改一遍 —— 正是这一票想避免的返工。
三个类型在 `context.h` 里只做**前置声明**（Context 只放指针，不需要完整类型），定义留给 08/09/10 各自建 `hal/imu.h` / `hal/audio.h` / `hal/network.h`。于是这一票确实「不实现任何服务」，但也不需要将来改 Context。

**改了什么**

- `include/embark/hal/context.h`：三个前置声明 + 三个可空指针成员（排在 `filesystem` 之后的尾部）；头注释重写「指针成员可空」一段，并写明为什么只前置声明（不让 Context 变成所有服务的公共依赖，也让「HAL 头里不许出现芯片型号名」天然成立）
- 三个装配点补齐到 11 项（服务槽本轮全 `nullptr`）：`platform/esp32/src/esp32_hal.h:67`、`platform/host/host_context.h:58`、`tests/fakes/fakes.h:35`
- 新增 `tests/hal/test_context.cpp`（4 个 TEST_CASE，注册进 `embark_tests`）：① 三个服务槽缺省 nullptr；② 聚合初始化只写 8 项时服务槽按值初始化成 nullptr（把「编译器不会拦」写成回归网）；③ 八个既有槽各自指向自己的后端（display / input / filesystem 都是裸指针、写反了编译器不报，这条逐个点名）；④ 服务槽为空时 `Framework` 照常 boot 且 App 读到 nullptr（08/09/10 接入前的基线）。另有 `static_assert(std::is_same_v<decltype(...imu), hal::IImu*>)` 三条把槽的类型钉在编译期 —— 前置声明（不完整类型）也能这么比，不需要实例
- 术语与文档：`GLOSSARY.md` 的 **HAL** 条目（去掉「不承担逐外设驱动」这个已被 0019 推翻的半句，改成「不承担业务语义」）、**HAL Context** 条目（去掉「七个」这个会过期的数字，补两段式结构 / 可空语义 / 「加成员编译器不会报错」这条坑）、**Service layer** 条目（补命名空间仍是 `embark::hal` 及其理由）；`docs/adr/0019` 顶部横幅从「代码尚未改 / 8 个成员」更新为「07 已落地骨架 / 11 个成员 / 接口只有前置声明」；`docs/reference/hooks-and-api.md` 的 Context 全量形状；`docs/concepts/index.md` 总图那格「七个能力接口的引用聚合」→「能力 + 服务的引用聚合（服务槽今天可空）」；`docs/guides/hal-backend-guide.md` 的 `context.h` 表行（七个成员 → 11 个）与装配段落

**实测**

- `cmake --build build` exit 0；`ctest` 2/2 通过；`embark_tests` **126 cases / 1368 assertions 全过**（07 之前 122 / 1346）
- `git grep -n "services/|embark::services"` = 0 命中（没另开命名空间）
- 五条验收故事与 `embark_host_tour --delay 40` 全 EXIT=0（见随后的全量回归）
