# 12 · 文档与新手最短路径

Status: resolved
Type: task
Blocked by: 08, 09

## 目标

spec §14.6 的文档验收：新人照着做就能跑起来、加一个 App。

## 范围

- 根 `README.md`：Embark 是什么、目录结构、宿主与 ESP32 两条构建命令、怎么加一个 App。
- `docs/README.md`：索引 + **三条最短路径**（跑起来 → 敲起来 → 改起来）。
- `docs/` 下补：HAL 后端怎么写（宿主/真机各一份骨架）、消息与后台策略怎么用、常见坑（ETL 定容、`lv_conf.h`、宏前置条件）。
- 追加 ADR 记录本轮决定：`docs/adr/0004-background-tick-and-state-policy.md`（后台节拍定 A、状态不绑范式、依赖先锁 20.40.0）。
- 与 `.scratch/embark-v1/spec.md`、`GLOSSARY.md` 交叉链接。

## 验收

- 一名没看过源码的人（或未来的自己）按最短路径能在 Windows 上跑起宿主 demo，并照着加一个只显示一行字的空 App。
- 文档里出现的命令都能直接复制执行（含 `export.ps1`、依赖初始化 submodule 那一步）。

## 备注

按本仓库文档约定：**代码标识符用英文，注释与文档用中文**。

## Answer

commit `2b1d8ab`（文档主体）+ 补号提交：

- 根 `README.md`：状态段补 issue 09/10；**ESP32-S3 构建**小节（当前占位说明 +
  issue 11 落地后的 `idf.py set-target esp32s3` / `idf.py build` 命令）；**怎么加一个
  App** 小节（四步概要 + 指向 docs/README.md 五步全版）；README 顶部文档链接、
  目录表 `docs/` 行指向 docs/README.md。
- `docs/README.md`（新增）：索引 + 三条最短路径——① 跑起来（submodule →
  cmake → build → ctest → 跑 UI demo，命令可复制）；② 敲起来（`--frames/--click/
  --switch/--own-task` 验收变体与退出码约定 0/1/2）；③ 改起来（照模板加一个只显示
  一行字的 App，五步 + 完整可复制代码）。
- `docs/messages-and-background.md`（新增）：消息两条路径（总线广播 / 收件箱信封）、
  消息类型定义与 id 分配（0xFE 保留、demo 0x21）、收/发示例、三种后台策略对照表
  与 AppSettings 写法。
- `docs/hal-backend-guide.md`（新增）：七能力接口 × 宿主实现 × 假实现的对照表、
  宿主后端骨架纪律（一对 host_*.{h,cpp}、无全局单例、错误显式、日志走门面）、
  ESP32 真机落地清单（七步）、"先假后端 → 再宿主 → 后真机"的省力顺序。
- `docs/common-pitfalls.md`（新增）：ETL 定容行为（满了=覆盖/计数/失败，不都报错）、
  消息类非聚合必须显式构造、0xFE 保留、无异常无堆、MinGW 无 std::aligned_alloc、
  Context 成员是引用、efmt 参数 ≤16、ELOG_WARN 无后端 no-op、宏前置条件
  （EMBARK_PLATFORM_HAS_FREERTOS / ETL_TARGET_OS_FREERTOS /
  ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK / lv_conf.h 单一来源）、宿主与测试特有坑。
- `docs/adr/0004-background-tick-and-state-policy.md`（新增）：后台节拍定方案 A
  （UI 任务内 callback_timer，周期向上取整、0=挂起）、状态不绑范式（state_chart
  只作推荐与示范）、own_task 是逃生舱不是变体、ETL 版本"先锁 20.40.0 后升 20.49.0"
  的时间线；Considered Options 与落地对照（issue 07/08、spec §6/§16）。
- **「改起来」按文档步骤实测落地**：`app/hello_app.{h,cpp}`（HelloApp：七钩子、
  只显示一行字、suspend、无状态无消息）加入构建与注册表（`EMBARK_APP_TABLE`
  末位，不参与验收断言）。重建零警告、ctest 全绿、clang-format dry-run 89 文件
  零不符；三个宿主验收变体全部 EXIT=0（--frames 60 --click / --frames 60 --switch /
  --frames 80 --click --switch），日志含 "框架就绪：4 个 App，默认前台 clock"。
- 交叉链接：README ↔ docs/README ↔ GLOSSARY ↔ spec（.scratch/embark-v1/spec.md）。
- 验收状态：本机可做的全部通过（构建/测试/宿主验收/dry-run/文档命令逐条可执行）；
  "没看过源码的人"路径按 docs/README 一段带跑完成（含 submodule、构建、跑 demo、
  加 App 全链路）。
- 备注 `export.ps1` 未在仓库出现（v1 无此文件），submodule 初始化命令已在
  docs/README 与根 README 双双给出。
