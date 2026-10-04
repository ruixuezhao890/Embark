# 09 · 测试套件与零分配审计

Status: resolved
Type: task
Blocked by: 06

Resolved: commit （提交后回填）

## Answer

- **doctest + ctest**：既有（`tests/`，单头、无堆友好）；现 **73 用例 / 495 断言全绿**，`ctest --test-dir build` 100%。
- **覆盖点对照 spec §13**：前台切换（`tests/kernel/test_framework.cpp`）、后台 tick 周期（`test_framework_messaging.cpp`，8 帧 1 次 / 16 帧 2 次 + period 0 = 挂起）、消息溢出策略（`test_message_queue.cpp` 满时覆盖最旧 + 溢出计数；`test_bus.cpp` 无人接收 WARN + 计数）、日志门面含串行化（`tests/hal/test_log_sink.cpp`：整行一次 write + 一次 flush、级别过滤、缓冲写满只计数、install_logger）、错误路径 `etl::expected`（fake 后端返回 `unexpected(...)` 的失败路径 + boot/spawn 错误传播）、App 注册表顺序（`test_app_registry.cpp`）。全部走 `tests/fakes/`，不依赖窗口。
- **零分配审计（本 issue 新增）**：
  - `tests/detail/zero_alloc_hooks.{h,cpp}`：重载全局 `operator new/new[]/delete/delete[]` 全家桶（普通、C++17 对齐版、sized 变体；MinGW 下对齐分配走 CRT `_aligned_malloc/_aligned_free`），只计数不拦截，计数放 BSS。全程序链接生效。
  - `tests/kernel/test_zero_alloc.cpp` 两个用例：① 内核稳态路径（构造 → boot → 8 帧 → publish 广播 → 切换 → shutdown）断言 **0 次堆分配**；② own_task 装配路径（成功 spawn + `no_space` 失败兜底）断言 0 次。取数前不走任何 doctest 断言，非 0 直接 `abort()`（stderr 写明次数）。
  - **钩子自检**：每条审计路径取数后主动 `::operator new` 一次，计数必须 +1 —— 证明"0 次"不是钩子失效的假象（审计本身被审计）。
  - **白名单：空**。内核契约就是"稳态路径无条件零分配"，无启动期一次性分配需要豁免（需要时按用例内 reset 分段 + 注明原因，v1 不需要）。
- 审计通过意味着：Framework 构造 / boot（bus 装配、callback_timer 注册、spawn 参数检查）/ step（tick、输入泵、切换、收件箱抽干）/ publish（Bus 镜像扫描 + ETL 分发）/ request_switch / shutdown 全程不碰堆 —— 这正是 `-fno-exceptions -fno-rtti` 下 ETL 定容容器承诺的落地证明。
- 未做（按工单备注）：无头 UI 截图测试，UI 靠宿主窗口人工验收。

## 目标

spec §13 的测试覆盖 + §14.4 的「内核零动态分配」可验证。

## 范围

- 接入 **doctest**（单头、无堆友好），`ctest` 可一键跑。
- 用例（对应 §13）：前台切换、后台 tick 周期、消息溢出策略、日志门面（含串行化）、错误路径（`etl::expected`）、App 注册表顺序。
- 全部测试走 fake 后端（issue 04），不依赖窗口。
- 零分配审计：宿主侧接管分配（重载全局 `operator new`/`delete` 与统计 `malloc` 调用），对「内核 + App 的稳态路径」断言分配次数为 0；允许启动期一次性分配的部位显式白名单并注明原因。

## 验收

- `ctest` 全绿。
- 零分配用例在 host 构建下断言 0 次；白名单有注释说明。
- 每个用例失败时能直接看出是哪个契约被破坏（断言消息写清楚）。

## 备注

不做无头 UI 截图测试（spec §13），UI 靠宿主窗口人工验。
