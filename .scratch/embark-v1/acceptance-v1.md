# Embark v1 验收清单

本清单对应 spec §14 的六条「完成的定义」，外加 issue 13（整对象日志）带来的新增项。
每一行都给**可自己复现的判定方式**。清单生成时的工作区状态：13 个 issue 全部
`Status: resolved`，工作区干净（`git status` 无输出），本地提交 25 个
（issue 13 = `655c1c7`，本清单是第 25 个提交），**从未 push**。

## 一、自动可复现的部分（我这边已全绿）

| # | 验收项（spec §14） | 判定方式 | 结果 |
| --- | --- | --- | --- |
| ① | 宿主构建后能跑出 SDL 窗口，demo 有 ≥2 个 App，可切换前台，后台 tick 可观测 | `build\platform\host\embark_host_ui.exe --frames 150 --click --switch --own-task` | EXIT=0。4 个 App（clock/settings/ticker/hello），收尾日志：`帧 150，刷新 65 次，Present 65 次，前台 clock（切换 2 次），clock ticks 7/brightness 1`；`ticker 发送 25 条 / UI 收到 25 条，总线发布 26 条 / 无人接收 0 条，收件箱溢出 0 次，UI 任务栈余量 2045 字`；可见 `App ticker 后台配置 embark::AppSettings { background = embark::BackgroundPolicy::own_task, period_ms = 50, task_stack_words = 256, task_priority = 4 }` 与成对的 `ticker 后台任务：embark::CrossTaskMessage { from_app = 2, seq = N }` |
| ② | 宿主测试全绿（时间相关断言只认 tick 相对量） | `E:\00_Software\03_Dev_Env\Cmake\bin\ctest.exe --test-dir build --output-on-failure` | `100% tests passed, 0 tests failed out of 1`；直接跑 `build\tests\embark_tests.exe` = **85 用例 / 629 断言全过** |
| ③ | ESP32-S3 目标构建通过 | `cmake --build build-esp32`（环境与命令见 `.scratch/embark-v1/evidence/13-format-derive/esp32-build.txt`） | EXIT=0；`embark_esp32.bin` = 0xbdf90 字节，分区 3 MB 余 75%；我们自己的代码零警告 |
| ④ | 内核与 App 无动态分配（hook 统计为 0） | 跑 `build\tests\embark_tests.exe`（全量），看 `零分配审计：boot→帧循环→消息→切换→shutdown 全程 0 次堆分配` 与 `零分配审计：own_task 装配路径（成功与失败兜底）0 次堆分配` 两条 | 通过。`tests/detail/zero_alloc_hooks.{h,cpp}` 拦 12 个 `operator new/delete` 重载，两条路径都是 0 次分配，且每条都带"钩子自检"（主动分配一次必须 +1，证明 0 不是假绿） |
| ⑤ | 换后端不动 App：切到 esp32 后端时 `app/` 与 `include/embark/` 一行不改 | `git show --stat 2134df3`（issue 11 真机后端那次提交） | 基本通过。真机后端只动 `platform/esp32/**` 与 `platform/common/**`；`include/embark/` 零改动。`app/demo_apps.cpp` 有 3 处改动，全部是 **xtensa 下 printf 类型的显式转换**（`uint32_t` 在 xtensa 是 `long unsigned int`，`%u` 会被 `-Werror=format` 拦下）—— 是编译器可移植性调整，不是后端耦合 |
| ⑥ | 文档到位：根 README + docs 索引 + 新手最短路径 | 打开 [docs/README.md](docs/README.md)；根 [README.md](README.md) | 通过。三条最短路径（跑起来 → 敲起来 → 改起来）就在 `docs/README.md` 开头，另加 `messages-and-background.md`、`hal-backend-guide.md`、`common-pitfalls.md`、`adr/0004-*` |

### issue 13 新增项（整对象日志）

| # | 验收项 | 判定方式 | 结果 |
| --- | --- | --- | --- |
| ⑦ | 类型的名字只有一份：登记处用派生宏，日志里不手拼字段 | `git grep -n "to_string" -- include src app platform tests` | 代码里零调用（只剩两条注释）；`Error`/`BackgroundPolicy`/`PixelFormat`/`InputEventKind` 用 `E_FMT_DERIVE_ENUM`，`Rect`/`DisplayInfo`/`InputEvent`/`AppSettings` 整对象派生，`CrossTaskMessage` 用类型体内 `E_FMT_FIELDS` |
| ⑧ | 输出口径被测试钉住 | 跑 `build\tests\embark_tests.exe`（全量），看 `派生打印：…` 五条用例 | 通过。`tests/kernel/test_format_derive.cpp` 5 个用例断言 9 个登记类型的完整输出串（如 `embark::hal::InputEvent { kind = embark::hal::InputEventKind::press, x = 160, y = 170, key = 113, timestamp_ms = 7 }`） |
| ⑨ | 日志行不超 384 字节上限（超了整行会消失） | 跑一次 UI demo，看 stderr 里 `HAL 就绪：显示 embark::hal::DisplayInfo { ... }` 与 `整屏区域 embark::hal::Rect { ... }` **各占一行** | 通过（这两行原本挤成一条时整行不打印，拆两条后正常；规矩写在 `docs/common-pitfalls.md`） |
| ⑩ | 代码格式与宿主/真机双构建零警告 | `clang-format --dry-run --Werror`（命令见 `.github/workflows/ci.yml` 的 format-check job） | 通过：117 个跟踪 C++ 文件零不符；宿主构建 warning/error 行数 0 |

> 小提示：`--test-case=` 过滤器从 PowerShell 传中文名字会因编码对不上而"0 ran"
> （不是测试挂了），要看单条用例就用 `--test-case-index=<N>`（编号从
> `--list-test-cases` 里数，1 起）或干脆跑全量 —— 全量只要 1 秒。
> 另外 demo 退出时会打一行 `UI 端口收尾：LVGL 未回收 NNNN 字节`：这是 `LV_MEM_CUSTOM 1`
> 下上游没有 `lv_deinit` 的必然结果（4 个 App 的界面对象都还活着），进程随即退出，
> 不是泄漏 —— 它的用途是给以后的回收/泄漏排查留一个观测点。

## 二、需要你在板子上人工确认的部分（我这边没有硬件）

spec §14 的 ③ 后半句「烧写后能显示 demo 的第一屏」与我给不出的触摸结论，
issue 11 已标 `⏳ 要上板人工确认`：

1. 烧写 + 看屏：`idf.py -C platform/esp32/project -B build-esp32 flash monitor`
   期望：第一屏是 clock App（时间 + Settings 按钮）。
2. 触摸 / 按键切前台：点 Settings 应切到设置屏；返回按钮回 clock。
3. 若方向或颜色不对：只动 `platform/esp32/src/esp32_board.h` 里那几个开关
   （启动日志会打印 CST328 自报的 `RES_X/RES_Y`，据此定轴方向）。
   判定与调法在 `platform/esp32/README.md` 的 bring-up 清单里。

⚠️ 本机 IDF 的 shell 导出（`export.ps1`）需要 `IDF_TOOLS_PATH` 与 venv 匹配的 python，
直接跑会报 `ESP-IDF Python virtual environment not found`；我走的是"直接驱动已配置好的
构建目录"这条路（命令逐行抄在证据文件里），你那边如果 `idf.py` 环境是好的，用 `idf.py` 更省事。

## 三、已知问题与待你拍板的事

### 1. 上游 efmt 的派生打印缺陷（我做了规避，没改上游）

- 现象：派生结构体里的 **1 字节整型成员**（`std::uint8_t`/`std::int8_t`/`unsigned char`）
  被当**字符**打印（值为 0 时写出 NUL，会把整行日志截断）；顶层 `std::uint8_t` 参数正常。
- 复现步骤、含 hex 的对照输出、上游代码定位（`format_traits.hpp:160-175`/`:133-155`、
  `format_derive.hpp:1952-1975`/`:2385-2429`）与手册自相矛盾之处（使用手册 `:270` vs `:320`）
  都在 [.scratch/embark-v1/evidence/13-format-derive/upstream-1byte-bug.md](.scratch/embark-v1/evidence/13-format-derive/upstream-1byte-bug.md)。
- 本仓库的规避：会进日志的 1 字节字段抬到 `std::uint16_t`（`AppId`、`task_priority`、
  `InputEvent::key`，以及两个 `spawn_task` 实现的 `priority`），字段语义不变。
- 没有改 `third_party/efmt-elog`：它是独立仓库、submodule 钉 commit，本地打补丁会让
  CI / 别人克隆时对不上，要改得走它自己的流程（而且推 upstream 也要你同意）。
  **要不要我按 efmt-elog 的 AGENTS 流程起草一份 issue（或直接给补丁）？**

### 2. 是否 push

本地 25 个提交都在 `main` 上，`origin` 还是空的（`https://github.com/ruixuezhao890/esp32-sim.git`）。
按你的规矩（push 必须先经你同意），我停在这里。**你说一声我就推，或者你自己推也行
（`git push -u origin main`）。**

## 四、一句话复现整套验收

```powershell
cd E:\01_Workspace\00_Active_projects\00_code\0_embedded_project\0_esp32_project\simulation_project_development
E:\00_Software\03_Dev_Env\Cmake\bin\cmake.exe --build build
E:\00_Software\03_Dev_Env\Cmake\bin\ctest.exe --test-dir build --output-on-failure
$env:SDL_VIDEODRIVER='dummy'
.\build\platform\host\embark_host_ui.exe --frames 150 --click --switch --own-task   # EXIT=0
.\build\platform\host\embark_host.exe                                              # EXIT=0（无头自检）
```

真机侧再补一条（环境见上文）：`cmake --build build-esp32`。

## 五、清单更新记录（2026-10-04）

本清单是最初生成时的快照，之后发生了下面这些变化，验收时以这里为准：

1. **测试基数**：② 的数字从 85 用例 / 629 断言变成 **86 用例 / 666 断言**（新增 issue 14 的
   无窗口系统用例"系统用例：从启动到任务切换走一遍"）。
2. **显示口径改成竖屏 240×320**（提交 `5d5765e`）：① 的窗口从横屏 320×240 改成竖屏
   240×320 —— 正是 ST7789 面板的原生方向（真机 ROT_NONE、宿主窗口同向），按钮中心坐标随之
   变成 `(120,220)`（Settings）与 `(120,265)`（Back to clock）；宿主 UI 与真机后端同步改，
   触摸轴口径也写进 `platform/esp32/README.md` 的 bring-up 清单。
3. **新增系统用例入口**（issue 14，提交 `9de835e` / `58bb4d4`）：`embark_host_tour.exe`
   不加参数就跑完"启动 → 后台节拍 → 切前台 → App 间消息 → 切回 → 关窗收尾"，控制台逐条中文
   解说 + 8 项自检清单，退出码 0 / 2；证据在 `evidence/14-system-tour/`（三份日志 + 一张
   切换后的画面）。
4. **任务创建策略有书面依据**：实际作法是"只用 `xTaskCreate*Static` + `heap_4.c` 不参与编译"，
   静态 vs 动态的逐项取舍记在 `docs/adr/0005-static-task-slots-and-pool.md`；"运行期创建 /
   回收任务"（首个用例：WiFi 非阻塞连接）作为后续工单记在 `issues/15-runtime-task-lifecycle.md`。
5. **③ 的真机构建数字**：最近一次全量重编 `embark_esp32.bin` = **0xbe240** 字节（升级后的 LVGL
   段全量链接进来；3 MB app 分区仍余 75%），bootloader 0x5210。
6. **已推送**：本地提交已在 2026-10-04 首次推送到 `origin/main`（`git push -u origin main`，
   仓库此前是空的）—— 第三节第 2 条的"是否 push"至此结案。
7. **启动器 + 导航壳 + App 元数据**（issue 16，提交 `ca42107`）：宿主 UI 现在以
   启动器为默认前台（注册表 5 条：Launcher/Clock/Settings/Ticker/Hello），扇形半环 6 槽；
   验收改用新入口 —— \`embark_host_ui.exe --launch\`（完整故事：拖 1 槽 → 点中槽启动 clock →
   Settings → Level +1 → 返回键回启动器，switches==3）、\`--drag x1,y1,x2,y2\`（合成拖动，
   断言目标槽）与 \`--click [x,y]\`；② 表格里的 \`--switch\` 已被 \`--launch\` 取代，历史坐标
   \`(120,265)\`（Back to clock，issue 16 移除自绘返回后随按钮删除）不再适用于宿主验收；
   测试基数 86 用例 → **108 用例**；tour 注册表 6 条（+启动器，Job 移到第 5 位）、14 项自检全过。

> 一句话：①–⑥、⑦–⑩ 的判定方式都没变，只是数字与窗口方向更新；第二节里"要你在板子上人工
> 确认"的两条仍然有效，且方向口径现在是**竖屏、不旋转**。

