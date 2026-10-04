# 13 · 整对象日志：用起来 E_FMT_DERIVE

Status: resolved
Type: task
Blocked by: 12
来源: 用户提问（"我查看了代码，为什么 efmt 中的 derive 宏一个都没使用"）

## 目标

让 efmt 的派生打印（`E_FMT_DERIVE` / `E_FMT_DERIVE_ENUM`）成为本仓库的**标准打印方式**：

1. 类型的名字只有一份 —— 写在类型声明处，不再手写 `to_string`、也不在日志里
   逐个字段拼 `{}`；
2. 加字段/加枚举值不用同步第二张名字表，日志自动跟上；
3. 用测试钉住输出口径，避免"改一处、日志悄悄变样"。

## 范围

- `include/embark/error.h`：`Error` 改 `E_FMT_DERIVE_ENUM`，删 `to_string` 声明，
  新增 `error_text(buffer, error)` 助手；删除 `src/embark/error.cpp`（连同
  `src/CMakeLists.txt` 的源列表项与真机组件的 `EMBARK_KERNEL_SOURCES` 项）。
- `include/embark/hal/types.h`：`Rect` / `DisplayInfo` / `InputEvent` 派生；
  `PixelFormat` / `InputEventKind` 派生枚举。
- `include/embark/app.h`：`AppSettings` 派生、`BackgroundPolicy` 派生枚举。
- `include/embark/message.h`：`CrossTaskMessage` 用类型体内的 `E_FMT_FIELDS`。
- 全仓库撤掉 `to_string` 调用点（宿主、ESP32、公共端口层、demo App、测试）。
- 整对象示范日志：framework boot（AppSettings）、ui_demo（DisplayInfo / Rect）、
  TickerApp 收发（CrossTaskMessage）、host 输入（InputEvent）、esp32 启动（DisplayInfo）。
- 派生输出里 1 字节整型成员会被当字符打（上游缺陷）⇒ 框架里会进日志的 1 字节
  字段抬到 `std::uint16_t`，并把缺陷与复现登记到证据目录。
- 文档：`docs/common-pitfalls.md`（384 字节单行上限 + 派生打印两节）、`docs/README.md`
  （用例数 + 日志说明）、根 `README.md`（日志小节）、spec §9 与新的 §17、
  `config/embark_config.h` 的 efmt/elog 上限登记。
- 新增 `tests/kernel/test_format_derive.cpp`。

## 验收

- `cmake --build build` 零警告；`ctest` 全绿（含新用例）；`clang-format --dry-run --Werror`
  零不符。
- 宿主验收变体全部 EXIT=0，且日志里能直接看到整对象：
  `embark::hal::DisplayInfo { ... }`、`embark::AppSettings { ... }`、
  `embark::CrossTaskMessage { from_app = ..., seq = ... }`。
- 全仓库 `git grep to_string` 只剩注释（代码零调用）。
- 上游缺陷有可复现的书面记录（含"顶层 uint8_t 正常、派生成员异常"的对照输出）。

## 备注

**不改 upstream**：`third_party/efmt-elog` 是独立仓库（submodule 钉 commit），
本地打补丁会让 CI/别人克隆时对不上；要改也得走它自己的流程（推 upstream 需用户同意）。
因此本仓库只做规避（字段抬宽）+ 登记（证据目录 + 测试），缺陷是否在 efmt-elog 修由用户定。

## Answer

工作区改动（本 issue 尚未提交时撰写；提交见本节末尾"提交"）。

### 1. 声明处登记（名字只有一份）

| 位置 | 类型 | 宏 |
| --- | --- | --- |
| `include/embark/error.h` | `Error` | `E_FMT_DERIVE_ENUM` + 新助手 `error_text()` |
| `include/embark/hal/types.h` | `Rect`、`DisplayInfo`、`InputEvent` | `E_FMT_DERIVE` |
| `include/embark/hal/types.h` | `PixelFormat`、`InputEventKind` | `E_FMT_DERIVE_ENUM` |
| `include/embark/app.h` | `AppSettings` | `E_FMT_DERIVE` |
| `include/embark/app.h` | `BackgroundPolicy` | `E_FMT_DERIVE_ENUM` |
| `include/embark/message.h` | `CrossTaskMessage` | 类型体内 `E_FMT_FIELDS(from_app, seq);`（有基类+构造函数，不能整体派生） |

`to_string(Error)` 的声明与实现（`src/embark/error.cpp` 的 34 行 switch）删除，
文件从仓库与构建里移除；只吃 `const char*` 的出口（`fprintf` / `embark::fatal`）
改用 `char text[24]; embark::error_text(text, error);`。全仓库 `to_string` 调用点
（宿主 main / ui_demo、公共 lvgl_port、esp32 main / display / input、demo App、测试）
逐个改完，`git grep` 现在只剩注释。

### 2. 1 字节整型成员的坑（上游缺陷 + 本仓库规避）

实测（GCC 15.1 / MinGW，`E_FMT_DERIVE` 默认开关）：

- 顶层参数正常：`std::uint8_t{5}` → `5`（hex 35）、`std::uint16_t{300}` → `300`；
- **派生结构体里的 1 字节成员异常**：
  `E_FMT_DERIVE(struct Small { std::uint8_t a; std::uint16_t b; std::int8_t c; unsigned char d; char e; });`
  → `Small { a = \x05, b = 300, c = \x06, d = \x07, e = x }`（原始字节，不是数字；
  值为 0 时写出 NUL，会把整行日志截断）。

据此把框架里会进日志的 1 字节字段抬到 `std::uint16_t`：`AppId`
（`invalid_app_id` → `0xFFFFU`）、`AppSettings::task_priority`、
`InputEvent::key`，以及跟着它们的 `ITaskSpawner::spawn_task` 的 `priority`
（宿主与 ESP32 两个实现同步）。字段语义不变（key 仍是 0..255 的键号）。

### 3. 附带发现：elog 单行 384 字节上限是"整行丢弃"

`ELOG_MAX_RECORD_SIZE` 默认 384 字节（`elog/elog.hpp:64`），前缀+正文放不下就
**整行丢弃**（`elog.hpp:186` 注释、`:211-220` 实现：不输出半行、不报错、不截断），
这块缓冲还是 `log_at` 的局部数组（每行占调用者栈 385 字节 ⇒ 与 `own_task_stack_words`
互相牵制）。整对象日志会显著拉长日志行，实测把 `DisplayInfo + Rect + 路径` 塞进
一条时**整行消失**，拆成两条后正常。处理方式：不调大上限，改成"一条日志一个整对象"
并在 `docs/common-pitfalls.md` / 根 README / spec §9 写明，配置头里登记该上限。

### 4. 整对象示范日志（都在跑的路径上）

- `src/embark/framework.cpp` boot：`App {} 后台配置 {}`（每个 App 一条 `AppSettings`）；
- `platform/host/ui_demo.cpp`：`HAL 就绪：显示 {}...`（`DisplayInfo`）+
  `整屏区域 {}`（`Rect`，拆两条）；
- `app/demo_apps.cpp` TickerApp：`ticker 后台任务：{}` / `UI 收到 ticker 消息：{}`（整个 `CrossTaskMessage`）；
- `platform/host/host_input.cpp` 键盘分支：`键盘事件 {}`（`InputEvent`）；
- `platform/esp32/project/main/main.cpp`：`显示 {}`（`DisplayInfo`）。

### 5. 测试与验证

- 新增 `tests/kernel/test_format_derive.cpp`（5 用例）：8 个登记类型的输出串
  （`embark::Error::not_found`、`embark::BackgroundPolicy::tick`、
  `embark::hal::PixelFormat::rgb565`、`embark::hal::InputEventKind::press`、
  `Rect` / `DisplayInfo` / `InputEvent` / `AppSettings` / `CrossTaskMessage` 的整对象串）
  + `error_text()` 与格式化结果一致。
- `cmake --build build` 零警告；`ctest --test-dir build --output-on-failure` 1/1 Passed
  （85 用例 / 629 断言全绿）；`clang-format --dry-run --Werror` 零不符。
- 宿主验收：`--frames 60 --click`、`--frames 60 --switch`、`--frames 80 --click --switch`、
  `--frames 150 --click --switch --own-task`、`--quit-at 30`、`--help` 全部 EXIT=0；
  无头自检 `embark_host.exe` EXIT=0。
- 真机目标也过一遍：`cmake --build build-esp32`（IDF 5.4 / esp32s3）EXIT=0，
  `embark_esp32.bin` = 0xbdf90 字节（分区 3 MB，余 75%），我们自己的代码零警告
  （只剩 LVGL 上游 `lv_obj_style.h:94` 的 `-Wdeprecated-enum-enum-conversion`）。
  第一次跑真机构建时暴露了一个漏改：真机组件自己的源列表里还留着
  `src/embark/error.cpp` ⇒ `Cannot find source file`，已在
  `platform/esp32/project/components/embark/CMakeLists.txt` 删除该项并把
  "内核源列表有两份"写进 `docs/common-pitfalls.md`。

### 6. 证据

`.scratch/embark-v1/evidence/13-format-derive/`：`derive-output.txt`（登记类型的
实测输出 = 测试期望串来源）、`host-demo-log.txt`（宿主 demo 的整对象日志片段）、
`upstream-1byte-bug.md`（1 字节成员缺陷的复现步骤与对照输出）、`esp32-build.txt`
（真机目标构建命令与结果，含那份漏改的报错原文）。
