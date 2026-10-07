# 变更日志

本仓库的**变更单元是 issue**：每个工单在 `.scratch/embark-v1/issues/` 里有完整记录
（问题 → 被否决的方案 → 取舍 → 验收证据）。这里只记**对外可见的变化**，以及"从上一版升级要动什么"。

版本号遵循 [SemVer](https://semver.org/lang/zh-CN/)。`0.x` 阶段 API 仍可能调整，
每次破坏性变更都会在下面写清**迁移方式**。

---

## [未发布] v0.1.0 —— 内核成形 + 双后端 + EEZ 适配

第一版可用的形态：**同一份源码**构建宿主（PC 仿真）与 ESP32-S3 两个目标。
尚未打 tag —— 真机界面观感确认与 RTC/SD 卡还没完成，见根 [README](README.md) 的「状态」。

### 新增

- **App 内核**（issue 06）：`App` 契约（4 个元数据 + 8 个生命周期钩子）、编译期静态注册表
  `EMBARK_APP_TABLE`、**全局唯一 UI 任务**、前后台切换（请求只登记、下一帧边界生效）。
- **消息与后台**（issue 07）：`Bus` 总线广播（同任务）+ `MessageQueue` 收件箱信封（跨任务）、
  `etl::callback_timer` 驱动的后台节拍、`own_task` 逃生舱。
- **三种后台策略**（issue 08）：`suspend`（默认）/ `tick`（按周期跑轻量逻辑）/
  `own_task`（独立任务，自带栈深与优先级）。
- **运行期任务生命周期**（issue 15）：`PooledTaskSpawner<Kernel>` —— 静态池分槽 + `xTaskCreateStatic*`，
  任务入口返回即结束、框架每帧回收槽位、`spawn_own_task()` 可在运行期再创建；
  归还走 `release_task(TaskToken)`（槽位下标 + 世代号，重复释放是稳定的 `not_found`）。
- **后台武装时机**（issue 23 / 24）：默认**第一次进过前台才武装**，另有 `ArmPolicy::at_boot` 开机即武装；
  观测接口 `background_armed(id)` / `arm_failures()`。
- **HAL 七能力**（issue 04）：`ITime` / `IPersistence` / `ILogSink` / `ISystem` /
  `IBus` / `IDisplay` / `IInput`，能力粒度到**芯片功能**，不做设备驱动。
- **宿主后端**（issue 05）：SDL2 显示/输入、LVGL 端口、文件持久化、FreeRTOS 静态接入、UI 任务。
- **ESP32-S3 真机后端**（issue 11）：ST7789（SPI）+ CST328（独立 I2C）、NVS 持久化、静态池 LVGL 堆，
  以 ESP-IDF 5.4 组件形式接入；板级参数与 bring-up 清单见 [platform/esp32/README.md](platform/esp32/README.md)。
- **整对象日志**（issue 13）：类型在声明处登记打印方式（`E_FMT_DERIVE` / `E_FMT_DERIVE_ENUM`），
  调用点只填空 —— 加字段不用改日志行，撤掉了手写 `to_string`。
- **零分配审计**（issue 09）：全局 `new`/`delete` 钩子 + 内核稳态路径断言 0 次分配。
- **CI**（issue 10）：宿主构建测试 / ESP32 固件构建 / clang-format 检查三个 job。
- **系统用例**（issue 14）：`embark_host_tour` 按 ①→⑨ 走完进程入口到关窗，17 项自检清单；
  另有不依赖窗口的 doctest 版本（`tests/kernel/test_system_tour.cpp`）。
- **EEZ Studio 适配**（issue 19）：界面全部交给 Studio 导出的屏，App 只调薄桥
  （`ensure_init` / `enter_app_screen` / `tick` / `set_var_*`）；
  屏名 == App 名、子页 `<app名>_<编号>_sub` 的约定；切屏事件翻成 `request_switch`。
- **屏生命周期**（issue 19 后续）：离开即回收、回来即重建（EEZ 的 Screens lifetime support），
  实测稳态仅 launcher ≈ 6.9 KB、+clock ≈ 8.0 KB（LVGL 预算 262144 B）。
- **脚手架**（`tools/scaffold_app.py`）：交互向导一键生成 App 薄壳 + `eez_vars.txt` 粘贴清单，
  支持增量加变量与 `--vars-check` 核对构建期变量表；默认 dry-run、可重复跑、锚点找不到就报错退出。
- **示例**：`examples/minimal/` —— 不依赖 EEZ、不依赖 demo App 的最小可运行 App（纯 LVGL 手写界面）。

### 变更（含破坏性）

- **每个 App 独立目录**（`app/<名字>/`）：原来的 `app/demo_apps/` 平铺结构取消，
  薄桥与消息类型归 `app/common/`。
- **导航壳退役**：框架不再持"状态行 + 统一返回键"的导航壳，回主屏由 EEZ 屏内按钮
  （SetPage → 屏观察者 → `request_switch`）承担。**迁移**：删掉 `fw.request_home()` 的调用点即可，
  接口本身还在（给显式返回用）。
- **`AppId` 从 1 字节加宽到 `std::uint16_t`**（issue 13 副作用）：
  上游 efmt 把 1 字节整型当字符打印，`0` 会写出 NUL 截断整行。框架里会进日志的字段一律 2 字节起。
- **后台日志改用 `{:#}`**：`AppSettings` 现在展开成多行并打印枚举**名字**（`tick`）而不是数字。
- **任务栈深单位是"字"不是字节**（issue 21）：`own_task_stack_words` / `ui_task_stack_words`
  按平台字长换算（宿主 1 字 = 8 字节、xtensa `StackType_t` 是 1 字节）。
  真机曾因照搬宿主数值只剩 1/8 栈而 panic。**迁移**：真机上的栈深请用 `stack_word_bytes` 换算后核对。
- **`own_task` 的日志不能压在任务栈上**（issue 22）：`ELOG_*` 的缓冲会吃掉栈预算，
  日志与栈深要一起算。

### 修复

- 日志单条上限 `ELOG_MAX_RECORD_SIZE`（默认 384 字节）超限是**整行丢弃**（不截断、不报错）
  ⇒ 一条日志只放一个整对象。
- 切换动画期间输入被 LVGL 屏蔽（`prev_scr != NULL` 时 `lv_indev` 直接返回）——
  脚本化点击必须排在 200 ms 动画窗口之后，这也是 `ui_demo` / `ui_tour` 帧号重排的原因。

### 文档

- **文档分三层**：`docs/concepts/`（为什么）/ `docs/guides/`（怎么做）/
  `docs/reference/`（具体值），总索引 [docs/index.md](docs/index.md)。
- 新增 [docs/reference/hooks-and-api.md](docs/reference/hooks-and-api.md)（8 钩子契约表 + API 全集）、
  [docs/reference/limits.md](docs/reference/limits.md)（全部容量常量）、
  [docs/concepts/index.md](docs/concepts/index.md)（分层总图 + 一帧数据流）、
  [docs/concepts/hal-and-portability.md](docs/concepts/hal-and-portability.md)、
  [docs/guides/scaffold-and-tools.md](docs/guides/scaffold-and-tools.md)。
- 新增 [CONTRIBUTING.md](CONTRIBUTING.md)；根 README 重写为面向新访客的开源首页。

---

## 里程碑（按时间）

每个里程碑对应一个 issue，编号可在 `.scratch/embark-v1/issues/` 里查到完整记录。

| 日期 | issue | 内容 |
| --- | --- | --- |
| 2026-10-03 | — | 仓库初始化：v1 规格书、术语表、ADR、agent skills |
| 2026-10-03 | 01 / 02 / 03 | ETL 升级到 20.49.0；宿主 FreeRTOS 移植验证；构建骨架 |
| 2026-10-04 | 04 / 05 | HAL 七接口 + 宿主基础后端 + 测试假后端；SDL2 显示/输入与 LVGL 接入 |
| 2026-10-04 | 06 / 07 | App 契约 + 唯一 UI 任务 + 前后台切换；消息总线 + 后台节拍 + own_task |
| 2026-10-04 | 08 / 09 / 10 | 三个 demo App；零分配审计；CI 三个 job |
| 2026-10-04 | 11 / 12 | ESP32-S3 真机后端；文档与新手最短路径 |
| 2026-10-04 | 13 / 14 | 整对象日志；系统用例（窗口版 + 无窗口版） |
| 2026-10-04 | 15 | 运行期任务生命周期（入口返回 = 结束，持有者回收槽位） |
| 2026-10-05 | 16 / 17 | 启动器扇形半环 + App 元数据；字体服务与缺字审计 |
| 2026-10-05 | 18 / 19 | EEZ Flow 技术验证；EEZ Studio 适配（转正为唯一的界面方案） |
| 2026-10-05 | 20 / 21 / 22 | 真机三个缺陷：IDF 组件源清单、任务栈单位、own_task 日志栈溢出 |
| 2026-10-06 | 23 / 24 | 后台武装时机：默认第一次进前台；新增 `ArmPolicy::at_boot` |
| 2026-10-06 | — | 导航壳退役；每个 App 独立目录；屏生命周期（离开即回收）；脚手架交互向导 |
| 2026-10-07 | — | 文档三层重组；`examples/minimal`；开源化（CONTRIBUTING / CHANGELOG / README） |

---

## 怎么用这份文件

- **想知道"某个机制为什么是这样"** → 去 `docs/adr/` 或对应 issue，那里写了被否决的方案。
- **想知道"升级到新版本要改什么"** → 看上面「变更（含破坏性）」一节。
- **想加一条变更** → 在 `[未发布]` 段落里按 `新增 / 变更 / 修复 / 文档` 分类追加，
  打 tag 时把 `[未发布]` 改成版本号与日期，并新开一段 `[未发布]`。
