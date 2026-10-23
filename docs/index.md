# Embark 文档

**先读这一篇：[guides/getting-started.md](guides/getting-started.md)** —— 一条从零到「跑起来 + 改一个界面」的路。
其余文档等卡住了再查，不读也能开始。

## 三层，按「你想知道什么」找

| 层 | 回答的问题 | 什么时候读 |
| --- | --- | --- |
| [**concepts**](concepts/index.md) | **为什么这么设计** —— 执行模型、一帧时序、后台策略、消息怎么走 | 想判断「这么写对不对」、准备改框架 |
| [**guides**](guides/getting-started.md) | **怎么用** —— 从零跑起来、加一个 App、画界面屏、写 HAL 后端 | 要动手做事 |
| [**reference**](reference/hooks-and-api.md) | **具体是多少 / 叫什么** —— 8 个钩子的契约、API 全集、容量上限、踩坑清单 | 写代码时查表 |

不在三层里的两处：[**adr/**](adr/)（决策当时的取舍与被否决的方案）、
[**agents/**](agents/README.md)（面向 AI agent 的仓库约定）。

## 30 秒：三条命令

```sh
cmake -G Ninja -B build                                   # 只在 build/ 还不存在时跑一次
cmake --build build --target embark_host_ui               # 编
cd build/host
./embark_host_ui.exe --frames 60 --screenshot probe.bmp   # 看（无头也看得见）
./embark_host_ui.exe --ui --delay 40 --frames 40          # 验（合成输入必须带 --delay 40）
```

**每天只碰三个地方**：界面 → `app/aidogui_ui/screen.aid`（改完 `pwsh -File tools/aidogui/export.ps1` 再编）；
App 逻辑 → `app/<名>/<名>_app.cpp`（直接编）；让 AI 画界面 → 一份 HTML 草稿走 `node tools/aidogui/html_to_aid.mjs`。
**不想敲命令**：双击 `tools/appconsole/start.cmd`，加 App / 退 App / 导出屏 / 构建宿主都在浏览器里点。
`app/aidogui_ui/export/` 是导出产物，**别手改**。

**之后每天**（改哪儿动哪个文件、跑哪几条验收、提交前自检什么）→ [guides/daily-loop.md](guides/daily-loop.md)。

## 两条赛道：基建 vs App

这个仓库有两条赛道，别混（决定「谁该动哪片代码」，见 [../AGENTS.md](../AGENTS.md) 与
[adr/0019-service-layer-and-public-hardware.md](adr/0019-service-layer-and-public-hardware.md)）：

| 赛道 | 归谁 | 动什么 |
| --- | --- | --- |
| **基建** | agent 做 | `core/include/embark/hal/`、服务层（`IImu` / `IAudio` / `INetwork`）、`host/`、`device/`、`core/cmake/`、`tools/`、`docs/` |
| **App** | **你写** | `app/<名>/` 里的业务逻辑，与 `app/aidogui_ui/screen.aid` 里的界面 |

agent 默认**只做基建**：不替你写 App 的业务逻辑与界面（要动必须你点名）。

## 索引

### concepts —— 原理

| 文档 | 内容 |
| --- | --- |
| [concepts/index.md](concepts/index.md) | **入口**：分层总图、一帧数据流、术语与三层边界 |
| [concepts/app-lifecycle.md](concepts/app-lifecycle.md) | **运行时总篇**：boot 装配顺序、唯一 UI 任务一帧 7 段、前后台切换何时生效、三种后台策略与**怎么申请**（判据表 + 能力矩阵）、own task 创建→回收全生命周期与三种写法、publish / post 两条消息路与消息类型、状态机接法、演示对照、源码索引 |
| [concepts/hal-and-portability.md](concepts/hal-and-portability.md) | HAL 为什么是这个粒度、宿主与真机怎么共用一份源码、新平台要提供什么 |

### guides —— 用法

| 文档 | 内容 |
| --- | --- |
| [guides/getting-started.md](guides/getting-started.md) | **唯一入口**：装工具 → clone → 配置 → 编 → 跑 → 最短验收 → 改一行就看见 → 五秒速查 |
| [guides/new-app-guide.md](guides/new-app-guide.md) | **加一个 App（给只会 Arduino 的人）**：C++ 生存包 → 一条命令建骨架 → **13 个可覆写点各该放什么** → 秒表完整例子（含 HTML 草稿画屏 + 验收读数）→ 后台策略判据 → 状态机与消息 → efmt / elog / etl → 红线表 → 排错表 |
| [guides/daily-loop.md](guides/daily-loop.md) | **日常循环（每天都用）**：一条命令起循环 → 「我要改什么 → 去哪」表 → 改界面的三条路 → 改完跑哪几条 → 提交前四条自检 → 「改了没反应」速查 |
| [guides/tools.md](guides/tools.md) | 十二个脚本各自干什么、验收开关全表、**App 管理台（网页控制台）**、给仓库加新脚本的规矩 |
| [guides/settings-and-preferences.md](guides/settings-and-preferences.md) | **设置中心（当前）**：设置屏三段版面、一个设置项怎么工作、偏好键表、加一个设置项的三步、五个坑 |
| [guides/state-machine-and-messages.md](guides/state-machine-and-messages.md) | 状态机与消息：`etl::fsm` 怎么接进 App 契约（收消息即切状态、运行语义与坑） |
| [guides/html-to-screen.md](guides/html-to-screen.md) | **画屏的另一条路（当前）**：写 HTML 草稿 → 无头浏览器量真坐标 → 合并进 `screen.aid` 指定屏；`data-*` 方言、默认不写盘、六屏往返自检；**让 AI 画草稿**看 §1（含可跑模板 [tools/aidogui/draft-template.html](../tools/aidogui/draft-template.html)） |
| [guides/hal-backend-guide.md](guides/hal-backend-guide.md) | 怎么写一个 HAL 后端：宿主骨架（照 `host/`）、共享层（`core/common/`）、真机实现（`device/`，含 IDF 坑清单） |
| [../app/aidogui_ui/README.md](../app/aidogui_ui/README.md) | **界面怎么改（当前）**：AiDoGUI 设计工程 → 一条命令重生成 → 编译；目录、字体与真机资源约定 |

### reference —— 查表

| 文档 | 内容 |
| --- | --- |
| [reference/hooks-and-api.md](reference/hooks-and-api.md) | **8 个钩子的契约表**（几次 / 哪个线程 / 该放什么 / 不该放什么）+ App、Framework、桥、HAL 的 API 全集 |
| [reference/ui-widgets.md](reference/ui-widgets.md) | **控件对照表**（自动生成）：6 屏 124 个控件的几何/文本/事件 + 代码里谁在用 + 死控件 / 孤儿引用清单 |
| [reference/limits.md](reference/limits.md) | `core/config/embark_limits.h` 全部容量常量的含义与「改它意味着什么」 |
| [reference/pitfalls.md](reference/pitfalls.md) | 常见坑：ETL 定容行为、消息非聚合、保留 id、无异常/无堆、MinGW 对齐分配、日志 384 字节上限、派生打印（`E_FMT_DERIVE`）、宏前置条件…… |

### 仓库里的其他文档

| 位置 | 内容 |
| --- | --- |
| [../README.md](../README.md) | 仓库总览：状态、构建、命令行开关、目录说明 |
| [../GLOSSARY.md](../GLOSSARY.md) | 术语表（Arm / Background tick / Own task / 薄壳……） |
| [../device/README.md](../device/README.md) | ESP32-S3 真机端口：板级参数、构建/烧录命令、bring-up 清单、串口日志样例 |
| [adr/](adr/) | 架构决策记录 19 条。**先读** [0019](adr/0019-service-layer-and-public-hardware.md)（服务层与公共硬件的硬规则）、[0015](adr/0015-app-retention-and-refresh.md)（留存与刷新）、[0011](adr/0011-aidogui-ui-source.md)（界面来源）、[0018](adr/0018-settings-truth-and-growth.md)（设置中心）；每篇文件头的 Status 写明它取代了谁、又被谁取代 |
| [../.scratch/](../.scratch/) | 规格书与 issue 追踪（随仓库提交，是设计决策的一手记录）：`embark-v1/`、`launcher-pages/`、`app-retention/`、`launcher-fixes/`，各有 `spec.md` + `acceptance.md` |

## 交叉参考

- **内核契约测试在 [../core/tests/kernel/](../core/tests/kernel/)**：发消息、切前台、节拍、own_task 生命周期与任务池装配的「正确用法」都在测试里，比任何文档都新。文档与测试冲突时以测试为准。
- 三层的关系：**concepts 说「为什么」**，**guides 说「怎么做」**，**reference 说「具体值」**。写代码时手边放 reference，读不懂设计动机时回 concepts。
