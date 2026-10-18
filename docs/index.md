# Embark 文档

文档分三层，按「你想知道什么」找入口：

| 层 | 回答的问题 | 什么时候读 |
| --- | --- | --- |
| [**concepts**](concepts/index.md) | **为什么这么设计** —— 执行模型、一帧的时序、后台策略、消息怎么走 | 想理解框架、要判断"这么写对不对"、准备改框架 |
| [**guides**](guides/quickstart.md) | **怎么用** —— 从零跑起来、加一个 App、画界面屏、写 HAL 后端 | 要动手做事 |
| [**reference**](reference/hooks-and-api.md) | **具体是多少 / 叫什么** —— 8 个钩子的契约、API 全集、容量上限、踩坑清单 | 写代码时查表 |

另外两处不在三层里：

- [**adr/**](adr/)：架构决策记录 —— 每条决策当时的取舍与被否决的方案（"为什么不是另一种做法"）。
- [**agents/**](agents/domain.md)：面向 AI agent 的仓库约定（领域模型、issue 追踪规则）。

## 30 秒上手

```sh
cmake --build build --target embark_host_ui               # 编（build/ 已 Configure，不用再 -G）
cd build/platform/host
./embark_host_ui.exe --frames 60 --screenshot probe.bmp   # 看（无头也看得见）
./embark_host_ui.exe --ui --delay 40 --frames 40          # 验（合成输入必须带 --delay 40）
```

**每天只碰三个地方**：界面 → `app/aidogui_ui/screen.aid`（改完 `pwsh -File tools/aidogui/export.ps1` 再编）；
App 逻辑 → `app/<名>/<名>_app.cpp`（直接编）；让 AI 画界面 → 一份 HTML 草稿走 `node tools/aidogui/html_to_aid.mjs`。
**不想敲命令**：双击 `tools/devpanel/start.cmd`，构建 / 导出 / 运行 / 看画面 / 跑验收 / 加 App 都在浏览器里点。
`app/aidogui_ui/export/` 是导出产物，**别手改**。**其余文档等卡住了再查**，不读也能开始。

---

## 第一次来：三步

**① 跑起来**（约 15 分钟，含装工具）→ [guides/quickstart.md](guides/quickstart.md)

**② 看懂它是怎么跑的**（可选，但推荐）→ [concepts/index.md](concepts/index.md) 的分层总图 + 一帧数据流

**③ 加一个自己的 App**（30 分钟，含画界面）→ [guides/new-app-guide.md](guides/new-app-guide.md)

**之后每天**：要改某处动哪个文件、跑哪几条验收、提交前自检什么 → [guides/daily-loop.md](guides/daily-loop.md)

```sh
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/platform/host/embark_host_ui          # Windows: .\build\platform\host\embark_host_ui.exe
```

看到 240×320 的竖屏窗口、里面是 launcher 屏、点按钮能切到 clock 屏 —— 就算跑起来了。

## 两条赛道：基建 vs App

这个仓库有两条赛道，别混（决定「谁该动哪片代码」，见 [../AGENTS.md](../AGENTS.md) 与
[adr/0019-service-layer-and-public-hardware.md](adr/0019-service-layer-and-public-hardware.md)）：

| 赛道 | 归谁 | 动什么 |
| --- | --- | --- |
| **基建** | agent 做 | `include/embark/hal/`、服务层（`IImu` / `IAudio` / `INetwork`）、`platform/`、`cmake/`、`tools/`、`docs/` |
| **App** | **你写** | `app/<名>/` 里的业务逻辑，与 `app/aidogui_ui/screen.aid` 里的界面 |

agent 默认**只做基建**：不替你写 App 的业务逻辑与界面（要动必须你点名）。
在你自己那条赛道上每天怎么干活，见 [guides/daily-loop.md](guides/daily-loop.md)。

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
| [guides/daily-loop.md](guides/daily-loop.md) | **日常循环（每天都用）**：一条命令起循环 → 「我要改什么 → 去哪」表 → 改界面的三条路 → 改完跑哪几条 → 提交前四条自检 → 「改了没反应」速查 |
| [guides/tools.md](guides/tools.md) | 十二个脚本各自干什么、验收开关全表、**开发台（网页面板：构建 / 运行 / 加退 App / 换界面工程 / 烧真机 / 看串口）**、给仓库加新脚本的规矩 |
| [guides/settings-and-preferences.md](guides/settings-and-preferences.md) | **设置中心（当前）**：设置屏三段版面、一个设置项怎么工作、偏好键表、加一个设置项的三步、验收命令与五个坑 |
| [guides/new-app-guide.md](guides/new-app-guide.md) | **新手指南（当前）**：30 分钟加一个带界面的 App（一条命令 `tools/new_app.mjs` 起步）（跑起来 → C++ 薄壳 → AiDoGUI 设计工程里画同名屏 + 补字符表 → 重导出 → 构建验收，端到端） |
| [guides/state-machine-and-messages.md](guides/state-machine-and-messages.md) | 状态机与消息：`etl::fsm` 怎么接进 App 契约（收消息即切状态、事件从 `onBackgroundTick` 喂、运行语义与坑） |
| [../app/aidogui_ui/README.md](../app/aidogui_ui/README.md) | **界面怎么改（当前）**：AiDoGUI 设计工程 → 一条命令重生成 → 编译；目录、字体与真机资源约定 |
| [guides/html-to-screen.md](guides/html-to-screen.md) | **画屏的另一条路（当前）**：写 HTML 草稿 → 无头浏览器量真坐标 → 合并进 `screen.aid` 的指定屏；`data-*` 方言、默认不写盘、六屏往返自检 |
| [adr/0011-aidogui-ui-source.md](adr/0011-aidogui-ui-source.md) | **界面来源决策（当前）**：BIN 资源走 S 盘、薄桥按需建屏、空屏不变量、三步迁移 |
| [adr/0012-weather-data-source.md](adr/0012-weather-data-source.md) | **天气数据来源（当前）**：立项时框架无网络 ⇒ 编译期桩；一处数据两处画；换成真数据的前瞻见 [0019](adr/0019-service-layer-and-public-hardware.md) |
| [adr/0013-launcher-paged-tiles.md](adr/0013-launcher-paged-tiles.md) | **主屏磁贴分页（当前）**：磁贴按注册表运行时生成（加 App 自动多一个）、吸附式横向翻页；`max_apps` 8 → 12。**非主屏上滑返回已被 [0014](adr/0014-in-screen-back.md) 撤回**，其余决策仍有效 |
| [adr/0014-in-screen-back.md](adr/0014-in-screen-back.md) | **屏内返回按钮（当前）**：撤回上滑返回，回程 = 每个 App 屏里的「返回」按钮；新增 `aidogui_ui_nav_bind_action`（控件 → 逻辑动作）与第 6 张屏 `settings`（**该屏形态已被 [0018](adr/0018-settings-truth-and-growth.md) 取代**，返回按钮与 `bind_action` 机制仍有效） |
| [adr/0016-runtime-font-rasterization.md](adr/0016-runtime-font-rasterization.md) | **运行期栅格化（历史→当前）**：TTF 走 stb 栅格化 + 每字号字形缓存，替掉 BIN 子集（第 2、5 条已被 [0017](adr/0017-font-ttf-from-sd.md) 取代） |
| [adr/0017-font-ttf-from-sd.md](adr/0017-font-ttf-from-sd.md) | **字体来源（当前）**：SD 卡上的整份 TTF、单 face、每字号一个槽、配额按公式 `max(4 KB, 8 × px²)` 算 |
| [adr/0018-settings-truth-and-growth.md](adr/0018-settings-truth-and-growth.md) | **设置中心（当前）**：亮度真值在 HAL、设置真值在 KV（`ui.` 前缀）、屏内交互归 App（滚动容器必须既 SCROLLABLE 又 CLICKABLE）；「关于」App 并入设置屏后退役 |
| [adr/0019-service-layer-and-public-hardware.md](adr/0019-service-layer-and-public-hardware.md) | **服务层（当前，取代 [0002](adr/0002-hal-capability-granularity.md)）**：HAL 之上加设备服务 + 公共资源（`IImu` / `IAudio` / `INetwork` / `IGpio`），业务服务不进基建；硬规则 **HAL 里不许出现芯片型号名** |
| [adr/0015-app-retention-and-refresh.md](adr/0015-app-retention-and-refresh.md) | **留存与刷新（当前）**：三条正交轴（留存申请 / 视图留存 / 数据新鲜度）+ 五类 App；后台变成显式申请的例外（今天全仓无人声明）、clock 与 launcher 退役 `tick` 走前台节拍、控件值由 `ui_backend_bind_repaint` 在屏装载后重推 |
| [guides/hal-backend-guide.md](guides/hal-backend-guide.md) | 怎么写一个 HAL 后端：宿主骨架（照 `platform/host/`）、共享层（`platform/common/`）与真机实现（`platform/esp32/`，含 IDF 坑清单） |

### reference —— 查表

| 文档 | 内容 |
| --- | --- |
| [reference/hooks-and-api.md](reference/hooks-and-api.md) | **8 个钩子的契约表**（几次 / 哪个线程 / 该放什么 / 不该放什么）+ App、Framework、桥、HAL 的 API 全集 |
| [reference/ui-widgets.md](reference/ui-widgets.md) | **控件对照表**（自动生成）：6 屏 124 个控件的几何/文本/事件 + 代码里谁在用 + 死控件 / 孤儿引用清单 |
| [reference/limits.md](reference/limits.md) | `config/embark_limits.h` 全部容量常量的含义与"改它意味着什么" |
| [reference/pitfalls.md](reference/pitfalls.md) | 常见坑：ETL 定容行为、消息非聚合、保留 id、无异常/无堆、MinGW 对齐分配、日志 384 字节上限、派生打印（`E_FMT_DERIVE`）、宏前置条件…… |

### 仓库里的其他文档

| 位置 | 内容 |
| --- | --- |
| [../README.md](../README.md) | 仓库总览：状态、构建、命令行开关、目录说明 |
| [../GLOSSARY.md](../GLOSSARY.md) | 术语表（Arm / Background tick / Own task / 薄壳……） |
| [../platform/esp32/README.md](../platform/esp32/README.md) | ESP32-S3 真机端口：板级参数、构建/烧录命令、bring-up 清单、串口日志样例 |
| [../.scratch/embark-v1/spec.md](../.scratch/embark-v1/spec.md) | v1 规格书（设计与约束的来源）；工单在 `../.scratch/embark-v1/issues/` |
| [../.scratch/launcher-pages/spec.md](../.scratch/launcher-pages/spec.md) | 主屏分页磁贴的规格 + 裁决表（Q1–Q10；其中上滑返回已按 [0014](adr/0014-in-screen-back.md) 撤回）；验收剧本 [acceptance.md](../.scratch/launcher-pages/acceptance.md)、工单在 `../.scratch/launcher-pages/issues/` |
| [../.scratch/app-retention/spec.md](../.scratch/app-retention/spec.md) | App 留存与刷新策略的规格 + 裁决表（Q1–Q4：屏自持 view state / 冻结型不申请后台 / 白名单靠自报 + 断言 / 前台节拍驱动）与验收剧本 [acceptance.md](../.scratch/app-retention/acceptance.md) |
| [../.scratch/launcher-fixes/spec.md](../.scratch/launcher-fixes/spec.md) | launcher 回程与两个界面 bug（时钟缓存、天气两字被裁）与设置屏的规格 + 验收剧本 [acceptance.md](../.scratch/launcher-fixes/acceptance.md) |

## 交叉参考

- **内核契约测试在 [../tests/kernel/](../tests/kernel/)**：发消息、切前台、节拍、own_task 生命周期与任务池装配的"正确用法"都在测试里，比任何文档都新。文档与测试冲突时以测试为准。
- 三层的关系：**concepts 说"为什么"**，**guides 说"怎么做"**，**reference 说"具体值"**。写代码时手边放 reference，读不懂设计动机时回 concepts。
