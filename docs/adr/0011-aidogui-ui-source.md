# AiDoGUI Designer 作为界面代码来源：薄桥按需建屏 + BIN 资源走 S 盘

用户 2026-10-12 拍板：把 EEZ Studio 换成 **AiDoGUI Designer**（理由是 EEZ 不适合 AI
开发——Studio 侧完全不可脚本化，改一个坐标必须人在 GUI 里点；见
[docs/guides/scaffold-and-tools.md](../guides/scaffold-and-tools.md) §1.5）。
AiDoGUI 的产物同样是 **LVGL C 代码**，所以 App 契约、屏名约定、控件表按名查找三条
不变，换掉的只是「生成代码从哪来」和「资源怎么进程序」。本 ADR 取代
[0008](0008-eez-studio-adapter.md)。

**状态**：已定。**字体那部分已被 [ADR 0017](0017-font-ttf-from-sd.md) 取代**：第 4 条里「字体 BIN 按
(字体文件, 字号) 各一个文件、只含实际用到的字符」与第 5 条里「生成代码用 `lv_font_load` 装 BIN」都已作废
—— 现在卡上放整份 `ui_heiti.ttf`、运行期栅格化，生成代码里那段字体装载被
`tools/aidogui/export.ps1` 打桩成空实现，只留 7 个 `lv_font_t*` 槽给桥去填。**图片仍走 BIN，那部分不变**。
本文其余决策（薄桥、屏表/控件表按名查找、S 盘）继续有效。

选型的实测依据（本次迁移跑出来的，不是手册说法）：工程文件 `screen.aid` 是 JSON、
9 个 MCP 工具可读写画布、代码生成器能在**没有 GUI**的情况下调用
（`tools/aidogui/export.ps1` 一条命令重导出，实测 `filesGenerated=21, warnings=[]`），
所以「AI 自己改界面 → 重导出 → 编译验收」这条闭环成立。

1. **工程与产物入库 + 一条命令重生成**：设计工程 `app/aidogui_ui/screen.aid`、
   字体源 `assets/font/`、预览图 `previews/`、AiDoGUI 原生导出目录
   `app/aidogui_ui/export/`（`ui.c`/`ui.h`/`screens/`/`components/`/`font/`/`image/`）
   **全部随仓库入库**——CI 与协作者不需要装 AiDoGUI（和 0008 决策 1 同一条理由，只是
   工具换了）。**屏表/控件表/字体表不入库**：`cmake/embark_aidogui_tables.cmake` 在
   CMake 配置期解析 `export/` 生成到构建目录（生成代码一变自动重跑配置），所以
   **「GUI 导出 → 直接编译」不需要任何额外脚本**。
   日常路径 = 设计器「打开工程」`screen.aid` → 编辑 → 导出向导（输出目录选
   `app/aidogui_ui/export`，图片/字体格式都选 BIN）；可选无头路径 =
   `pwsh -NoProfile -File tools/aidogui/export.ps1`（清空并重建 `export/`）。设计器
   位置三级查找：`AIDOGUI_HOME` → `.aidogui-home` → `tools/aidogui/home.txt`（最后
   一个是本机私有路径，已 gitignore）；设计器的 `.ai-workspace/`（含本机桥 token）也
   已 gitignore。clang-format 门禁把 `app/aidogui_ui/` 整体排除（导出即覆盖，与
   `app/eez_ui/` 同一口径）。
2. **不调用生成的 `aidogui_init()`**：它一次性创建**全部**屏且从不回收，字体加载
   失败还直接 return（连屏都不建），与本框架「切走即释放」纪律冲突。桥
   （`app/common/aidogui_ui_bridge.{h,cpp}`）自己承担它里面的三件事：注册
   `LV_EVENT_GET_COMP_CHILD`、加载字体、按需调用 `ui_<屏id>_screen_init()` /
   `ui_<屏id>_screen_destroy()`。字体加载失败只记警告并降级（该字号退回内置字体），
   绝不让整屏失效。
3. **空屏不变量（LVGL 8 的坑，写进桥的契约）**：删掉当前屏后 LVGL 只把
   `disp->act_scr` 置 NULL（`lv_obj_tree.c` 的 "the active screen was deleted" 警告），
   而 `lv_scr_load()` 会对 `lv_scr_act()` 调 `lv_obj_set_pos()` → 空指针访问冲突。
   实测：修复前 `--ui` 验收在第 6 步「释放后再装载」崩（退出码 0xC0000005），
   修复后通过。所以桥**终身持有一张空屏**（懒建、纯色底）：释放真屏前先把显示器
   交给它，`aidogui_ui_bridge_destroy()` 也不删它——保证「显示器上永远有一张有效的
   当前屏」。真机上它同时就是「没有 App 屏可显示」时的兜底画面。
4. **资源格式 = BIN，路径写死 `S:/fonts` 与 `S:/images`**：生成代码不管
   `exportSettings.*BinRootPath` 填什么，一律加载 `S:/fonts/<name>.bin`、
   `S:/images/<name>.bin`（实测）——于是**宿主与真机各挂一个 S 盘，两端零差异**：
   宿主由 `platform/common/aidogui_res_fs` 把 `S:` 指到 `app/aidogui_ui/export`（生成
   代码写复数目录 `fonts/`/`images/`，AiDoGUI 磁盘上是单数 `font/`/`image/`，桥做目录名
   兜底 ⇒ 导出完不用改名），真机是 SD 卡挂载点（第 3 步）。**为什么不是 C 数组**：同工程实测 C 模式
   46.16 MB / BIN 10.94 MB，其中单张图 3081 KB vs 616 KB ≈ 5.0 倍，这只是「文本 vs
   二进制」的表示差——编译进固件后两者占的 flash 基本相同，所以「BIN 省 flash」在
   同一块 flash 上不成立；真正的理由是用户板上**资源要放 SD 卡、读进 8 MB PSRAM**：
   把资源移出 `.rodata`，缩小 app 分区与 OTA 包。代价是必须有文件系统（真机侧
   第 3 步补）。字体 BIN 按 **(字体文件, 字号)** 各一个文件、只含**实际用到的字符**
   （桌面屏 6 个字号合计约 4.7 KB）。
5. **LVGL 版本口径 = 工程里的 8.4.0，仓库 LVGL 仍 8.3.11**：8.3.11 有
   `lv_font_load`/`lv_font_free`（`src/font/lv_font_loader.h`），生成代码的 8.4.0
   分支用的正是这一对；9.x 分支才走 `lv_binfont_create`（仓库没有）。工程里不要改
   这个字段，也不要为了编辑器升 LVGL——理由与 0008 否决项同。
6. **数据接口 = 控件表按名查找**：`aidogui_ui_bridge_widget(screen, widget)` 拿
   `lv_obj_t*`（表由 cmake 在配置期解析 `export/` 生成，见决策 1）。App **不直接引**
   `ui_<屏id>_<控件名>` 全局——那样在 AiDoGUI 里改名会编译期静默断链。屏名约定不变：
   **screen id == App 名**（子页 `<app名>_<编号>_sub`），App 只按名字找自己的屏。
   注意：重建屏时不要「改名」——改名必须同步改所有子控件的 `parentId`，漏改的控件
   会被静默丢弃；正解是删掉旧屏按目标 id 重建。
7. **切屏仍归框架；导航在设计器里声明、在桥上实现（第三次验收定稿）**：`CHANGE_SCREEN`
   动作最终走生成代码的 `_ui_screen_change()`（只换屏，不销毁旧屏、不通知框架），
   **不能用来做导航**——它会绕过桥的按需 init/destroy 纪律（决策②）。正解是设计器里的
   「**调用函数**」动作：函数名约定 `embark_ui_open_<App 名>`，生成代码原样吐一句裸调用
   （`embark_ui_open_clock();`），落到 `app/common/aidogui_ui_hooks.h` 声明的 C 入口——
   该头由 `app/aidogui_ui/CMakeLists.txt` 用 `-include` **强制注入**每个生成 C 文件，
   所以生成代码仍然只读、不用手改。C 入口（X 宏表 `EMBARK_AIDOGUI_HOOK_APPS`）全部转发到
   `aidogui_ui_nav_request_app()` → `framework.request_switch`（帧边界生效）。
   **为什么绕这一圈**：导航意图（点哪个控件去哪一屏）属于界面，画在设计器里才看得见；
   而「切屏 = 切前台 App」的纪律必须只有一处实现。屏名 → App 名的解析与「够不够切」仍走
   观察者（`app/common/aidogui_ui_nav.cpp`）。三条事件：launcher `Tile1` →
   `embark_ui_open_clock`、`Tile2` → `embark_ui_open_settings`、clock `Btn_Back` →
   `embark_ui_open_launcher`；备用路径 `aidogui_ui_nav_bind_click(屏名, 控件名, App 名)`
   仍在（不想在设计器里挂事件时用，App 里声明一次、控件随屏销毁消失由观察者重挂）。
   `tools/aidogui/export.ps1` 会数生成代码里的 `embark_ui_open_*()`：0 条大声告警
   （事件丢了只会"点了没反应"），调用了 hook 头里没声明的名字直接报错。
   **关键坑（第二次验收的根因，实测记录）**：桥的公开入口
   `aidogui_ui_bridge_load_screen()` 原本调 `load_entry(entry, /*notify=*/false)`，
   于是观察者**从不执行** ⇒ 点击绑定从不重挂。现象很有误导性：合成点击完全正常地进了
   LVGL（`indev->proc.state == 1`、`proc.types.pointer.act_obj` 就是那个磁贴），
   但我们挂在控件上的回调一次都没跑、`--ui-nav` 的命中数恒 0；连在验收台里直接
   `lv_event_send(tile, LV_EVENT_CLICKED, nullptr)` 也是 0 命中——「回调根本没挂上」和
   「事件没送到」这两件事必须靠这个实验区分。改成 `notify=true` 后双向导航全绿。通知
   桥自己发起的装载**不会成环**：观察者顺手请求切到同名 App 时，框架对「已是当前前台」
   的请求直接 return（`src/embark/framework.cpp` 的 `apply_pending_switch` 第一句就是
   `if (pending_ == foreground_) return;`），所以不会重复 onEnter。跨 App 切换只有框架
   一个出口（与 0006/0008 决策 3 一致）。
8. **内存**：字体与控件全走 LVGL 堆（宿主 = 256 KB 记账预算；真机 =
   `platform/esp32/src/esp32_board.h` 的 `lvgl_pool_bytes` 48 KB 静态池）。本轮
   **不动内存模型**：两块屏 7 个字体 BIN 合计约 24.6 KB（桌面屏 6 个字号 4.7 KB；clock
   屏加了 48px 大字号 + 显式字符表，见决策 11），装得下。真机把资源放进 PSRAM 是
   第 3 步的方向——若 PSRAM 也要给 LVGL 用，正解是让 `g_lvgl_pool_arena` 从 PSRAM
   分配，而不是自写零拷贝加载器绕开 LVGL 的字体/图片接口。
9. **迁移节奏（三步，每步可验收）**：①宿主一屏跑通（launcher：屏表 + 控件表 + 字体 +
   切走即释放 + 重新装载，验收 `embark_host_ui --ui`，本次已完成）；②AiDoGUI 里画
   clock / settings 两屏接上（**clock 已完成**：25 个控件 + 「返回」回程 + 真数据上屏，
   验收 `embark_host_ui --ui-nav`，剩 settings），然后删除 `app/eez_ui/`、两个 cmake 代码表
   模块、EEZ 文档与 CI 例外，0008 标废弃；③真机立项：TF 槽引脚核实 → `esp_vfs_fat` → 新
   HAL 存储面 → SD → PSRAM → 字体/图片从 PSRAM 取用 → 加载失败降级 + 验收探针。
10. **字体源**：本轮沿用 `ui_heiti.ttf`（「时尚中黑简体」，商业授权待确认）；要用在
   正式产品里换成 OFL 的 Noto Sans SC 更稳（换字体 = 换工程里的 font 引用 + 放 ttf +
    重生成，字号与子集化自动跟着走）。
11. **字体子集必须显式声明字符表（否则动态数字变方框）**：AiDoGUI 默认按**设计期文字**
    给每个 (字体, 字号) 子集化——clock 屏设计稿里大时间是 `11:31`，于是 48px BIN 里只有
    `{1,3,:}` 三个字形，运行时显示 `23:57` 的 0/2/5/7 **全成方框**（截图实测；修复后
    同一帧截图正常）。正解是用设计器自带的「自定义字符」表把运行期字符写进工程：
    `i18n.customCharsetEnabled = true` + `i18n.customCharsetRows[]`，每行
    `{fontName, fontSize, symbols, range, enableFontAwesome}`（GUI 在「语言 / 自定义字符」
    页维护，工程 JSON 里也一目了然）。**代价**：开了这个开关后生成器**不再扫描设计期
    文字**，所以每行必须是「设计期文字 ∪ 运行期字符」的超集，而且**每个 (字体, 字号)
    都必须有一行**，否则导出直接报「字体 X (Npx) 未配置自定义字符范围」。当前 7 行 =
    11/12/14/22/28/48/56px，字符数 12–61，字体 BIN 合计 **24,572 B**（48px 每字形约
    1.2 KB，是总变大的主因）。换字体、加字号、加会显示动态数字的控件时，这张表要跟着改。

**验收证据（第 1 步，2026-10-12）**：`embark_host_ui --ui --frames 40` 打
〔aidogui 验收通过〕（退出码 0），六条证据 = 字体 6/6 个 BIN 加载成功、屏表 1 屏、
当前屏 launcher 已创建且 23 个控件、按名取 `launcher/L_Time` 文本 = `11:31`、
`unload_current()` 后屏已释放且控件指针归 nullptr、重新装载后 23 个控件回来且文本仍是
`11:31`；同一次运行的第 25 帧截图（`.scratch/aidogui_ui.png`）中文与布局正确 =
生成代码 + BIN 字体端到端可用。

**复验（同日，导出布局改成 `export/` + 表改由 CMake 配置期生成之后）**：同样六条全绿、
退出码 0，`ctest --test-dir build` 1/1 通过；运行日志确认 S: 盘指向
`app/aidogui_ui/export`、字体 6/6 加载、屏表 1 屏（launcher，23 控件）。
「GUI 一次导出 → CMake 配置期建表 → 直接编译验收」这条链路由此闭合。

**验收证据（第 2 步 · clock 屏，2026-10-12 深夜）**：`embark_host_ui --ui-nav --delay 20`
退出码 0，末行〔aidogui 导航验收通过〕（前台 launcher，switches=2，点击命中 2 次）：点启动器
磁贴 → 屏 launcher 释放 → 屏 clock 装载（25 个控件）→ `L_Time` = 实时时间、`Bar_Prog` = 38
（当月进度百分比）→ 点「返回」→ 屏 clock 释放 → 屏 launcher 装载（23 个控件）。同一次交付里
`--ui --frames 40` 退出码 0（字体 7/7、屏表 2 屏），`ctest --test-dir build` 1/1 通过。
第 24 / 42 帧截图 `.scratch/ui_nav_clock.png`、`.scratch/ui_nav_back.png` 确认中文、实时数字、
月历高亮、进度条与往返都正确。

**验收证据（第 2 步 · 导航改由设计器「调用函数」事件驱动，2026-10-13 凌晨）**：
`export.ps1` 打印「导航事件：embark_ui_open_clock x1 · embark_ui_open_launcher x1 ·
embark_ui_open_settings x1」，生成代码里 `ui_event_launcher_Tile1_0` / `ui_event_clock_Btn_Back_0`
的 CLICKED 分支就是这几句裸调用。`embark_host_ui --ui-nav --delay 20` 退出码 0、
〔aidogui 导航验收通过〕（前台 launcher，switches=2，**导航命中 2 次**；日志里两条
`AiDoGUI 导航：请求切到 App …` 来自 `aidogui_ui_nav_request_app()`，C++ 侧点击绑定 0 条）；
`--ui --frames 40` 退出码 0（字体 7/7、屏表 2 屏）、`ctest --test-dir build` 1/1 通过。
第 24 / 42 帧截图 `.scratch/ui_hooks_clock.png`、`.scratch/ui_hooks_back.png`。

**否决过的方案**：在设计器里给控件挂 `CHANGE_SCREEN` 事件做导航（`_ui_screen_change()`
绕过桥的按需 init/destroy 纪律，见决策⑦，导航改在 C++ 侧声明式绑定）；保留 EEZ（Studio
不可脚本化，AI 改不了界面——正是本次要解决的痛点）；
C 数组资源（资源进 `.rodata`，与「SD 卡 + PSRAM」的既定方向相反）；App 直接引
`ui_<id>` 全局（改名静默断链）；图省事直接调 `aidogui_init()`（一次建全屏不回收 +
字体失败连屏都不建）；为编辑器升 LVGL 9（版本升级是独立决策，不因工具绑架）；
把 `.aid` 当只能由 GUI 改的二进制工程（它其实是 JSON，允许脚本化生成/编辑，但**唯一
验收是重导出（GUI 向导或 `export.ps1`）+ 编译 + `--ui` 探针**——手改出错的 .aid 只有
在导出/渲染时才看得出来）。

**代价与影响**：新增工具链 `tools/aidogui/`（Windows PowerShell + AiDoGUI 安装目录，
非 Windows 协作环境只能吃已入库的产物）；`app/eez_ui/` 与 `third_party/eez-framework`
在第二步退役（0008、`docs/guides/eez-*.md`、两个 cmake 代码表模块、CI 例外随之删除）；
真机在第三步之前**仍然没有界面代码**（宿主是唯一验收环境）；BIN 资源意味着真机必须
先有文件系统，加载失败要能看到降级日志而不是空屏。

**交叉阅读**：[0006](0006-launcher-and-nav-shell.md)（启动器与回程）、
[0007](0007-font-management.md)（字体红线）、[0008](0008-eez-studio-adapter.md)（被取代的
适配层决策）、`app/aidogui_ui/README.md`（改界面 → 重生成 → 编译的操作手册）、
`app/common/aidogui_ui_bridge.h`（桥的接口全集）。
