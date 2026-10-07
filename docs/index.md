# Embark 文档

文档分三层，按「你想知道什么」找入口：

| 层 | 回答的问题 | 什么时候读 |
| --- | --- | --- |
| [**concepts**](concepts/index.md) | **为什么这么设计** —— 执行模型、一帧的时序、后台策略、消息怎么走 | 想理解框架、要判断"这么写对不对"、准备改框架 |
| [**guides**](guides/quickstart.md) | **怎么用** —— 从零跑起来、加一个 App、画 EEZ 屏、写 HAL 后端 | 要动手做事 |
| [**reference**](reference/hooks-and-api.md) | **具体是多少 / 叫什么** —— 8 个钩子的契约、API 全集、容量上限、踩坑清单 | 写代码时查表 |

另外两处不在三层里：

- [**adr/**](adr/)：架构决策记录 —— 每条决策当时的取舍与被否决的方案（"为什么不是另一种做法"）。
- [**agents/**](agents/domain.md)：面向 AI agent 的仓库约定（领域模型、issue 追踪规则）。

## 第一次来：三步

**① 跑起来**（约 15 分钟，含装工具）→ [guides/quickstart.md](guides/quickstart.md)

**② 看懂它是怎么跑的**（可选，但推荐）→ [concepts/index.md](concepts/index.md) 的分层总图 + 一帧数据流

**③ 加一个自己的 App**（30 分钟，含画界面）→ [guides/new-app-guide.md](guides/new-app-guide.md)

```sh
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/platform/host/embark_host_ui          # Windows: .\build\platform\host\embark_host_ui.exe
```

看到 240×320 的竖屏窗口、里面是 launcher 屏、点按钮能切到 clock 屏 —— 就算跑起来了。

## 索引

### concepts —— 原理

| 文档 | 内容 |
| --- | --- |
| [concepts/index.md](concepts/index.md) | **入口**：分层总图、一帧数据流、术语与三层的边界 |
| [concepts/app-lifecycle.md](concepts/app-lifecycle.md) | **机制原理**：boot 装配顺序、唯一 UI 任务一帧 7 段、前后台切换何时生效、三种后台策略、own task 创建→回收全生命周期、publish/post 两条消息路 —— 配图 + 源码索引 |
| [concepts/messages-and-background.md](concepts/messages-and-background.md) | 消息（总线 / 收件箱信封）与三种后台策略**怎么用**，含示例代码；怎么选策略的判据表；own_task 的生命周期 |
| [concepts/hal-and-portability.md](concepts/hal-and-portability.md) | HAL 为什么是这个粒度、宿主与真机怎么共用一份源码、新平台要提供什么 |

### guides —— 用法

| 文档 | 内容 |
| --- | --- |
| [guides/quickstart.md](guides/quickstart.md) | **从零跑起来**：装工具 → clone → 构建 → 跑 → 最短验收 → 加一个 App 的入口 |
| [guides/new-app-guide.md](guides/new-app-guide.md) | **新手指南**：30 分钟加一个带界面的 App（建壳 → 画同名屏 → 绑变量 → 构建验收，端到端） |
| [guides/eez-ui-manual.md](guides/eez-ui-manual.md) | EEZ UI 用户手册：界面交给 EEZ、App 只调几个接口（薄桥 API 全集 + 命名约定 + 模板） |
| [guides/eez-studio-guide.md](guides/eez-studio-guide.md) | EEZ Studio 一条龙：装 Studio、建工程、导出代码入库（源工程随库入库） |
| [guides/hal-backend-guide.md](guides/hal-backend-guide.md) | 怎么写一个 HAL 后端：宿主骨架（照 `platform/host/`）、共享层（`platform/common/`）与真机实现（`platform/esp32/`，含 IDF 坑清单） |
| [guides/scaffold-and-tools.md](guides/scaffold-and-tools.md) | 脚本与工具：`tools/scaffold_app.py` 生成 App 薄壳、字体子集生成、验收开关与退出码 |

### reference —— 查表

| 文档 | 内容 |
| --- | --- |
| [reference/hooks-and-api.md](reference/hooks-and-api.md) | **8 个钩子的契约表**（几次 / 哪个线程 / 该放什么 / 不该放什么）+ App、Framework、桥、HAL 的 API 全集 |
| [reference/limits.md](reference/limits.md) | `config/embark_limits.h` 全部容量常量的含义与"改它意味着什么" |
| [reference/pitfalls.md](reference/pitfalls.md) | 常见坑：ETL 定容行为、消息非聚合、保留 id、无异常/无堆、MinGW 对齐分配、日志 384 字节上限、派生打印（`E_FMT_DERIVE`）、宏前置条件…… |

### 仓库里的其他文档

| 位置 | 内容 |
| --- | --- |
| [../README.md](../README.md) | 仓库总览：状态、构建、命令行开关、目录说明 |
| [../GLOSSARY.md](../GLOSSARY.md) | 术语表（Arm / Background tick / Own task / 薄壳……） |
| [../platform/esp32/README.md](../platform/esp32/README.md) | ESP32-S3 真机端口：板级参数、构建/烧录命令、bring-up 清单、串口日志样例 |
| [../.scratch/embark-v1/spec.md](../.scratch/embark-v1/spec.md) | v1 规格书（设计与约束的来源）；工单在 `../.scratch/embark-v1/issues/` |

## 交叉参考

- **内核契约测试在 [../tests/kernel/](../tests/kernel/)**：发消息、切前台、节拍、own_task 生命周期与任务池装配的"正确用法"都在测试里，比任何文档都新。文档与测试冲突时以测试为准。
- 三层的关系：**concepts 说"为什么"**，**guides 说"怎么做"**，**reference 说"具体值"**。写代码时手边放 reference，读不懂设计动机时回 concepts。
