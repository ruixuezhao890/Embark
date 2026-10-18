# 11 · 一键生成 App（AiDoGUI 版脚手架）

Status: done（2026-10-18）
Blocked by: —（但必须在 01 之后做：01 删掉了 EEZ 版脚本）

## 为什么

01 号票把 `tools/scaffold_app.py`（594 行）与它的测试（153 行）删了 —— 那份脚本生成的是 EEZ 时代的骨架：
`#if defined(EMBARK_EEZ_UI_BRIDGE)` + `eez_ui_bridge_enter_app_screen()` + `eez_ui_bridge_tick()`，而那个头已经不存在，
**生成出来的 App 挂不上屏**。用户裁定（m02283）：「想要『一键生成 App』可以另立票写成 AiDoGUI」。

所以这一票不是恢复旧脚本，是新写一份「照今天的真身生成」的脚本。

## 设计决定：脚本不内嵌模板，运行期读 `app/music/`

旧脚本的根本毛病是**模板在脚本里另存一份**，于是 App 真身往前走、模板留在原地，生成物必然走偏（旧脚本连 `policy` 分支都重复写了两遍，会插两次 `settings()`）。

新的做法只有一份真相：

- 模板 = 仓库里的 `app/music/music_app.h` + `app/music/music_app.cpp`（121 行，编译通过、跑过验收的那一份）
- 脚本 = 读它们 → 做标识符与文案替换 → 写 `app/<name>/<name>_app.{h,cpp}` → 登记 4 处

于是**没有第二份模板可以漂移**。代价：如果将来 music 的形状变了（比如薄桥改名），脚本跟着变 —— 这正是想要的。

`--self-check` 子命令用于把这件事做成硬检查：断言 music 的两个文件里仍然存在脚本依赖的那些标记串（`class MusicApp final : public App`、
`name() override`、`ui_backend_load_for_app`、`ui_backend_tick`、`EMBARK_AIDOGUI_UI` 等），缺一个就报错退出。
CI 不跑它（本仓 CI 不跑任何 Node 脚本），但改 music 的人手工跑一次就知道脚本还活着没有。

## 要做的

**① 新增 `tools/new_app.mjs`**（Node ESM，与 `tools/font/*.mjs` 同一风格；不用 Python）

CLI：

```
node tools/new_app.mjs <app名> [--title 中文名] [--icon LV_SYMBOL_X] [--dry-run] [--apply]
node tools/new_app.mjs --self-check
```

- 位置参数：App 名，必须 `^[a-z][a-z0-9_]*$`；**拒绝以 `_sub` 结尾**（屏名约定里那是子页后缀，见 `app/common/ui_screen_names.h`）；已存在同名目录则拒绝
- `--title`：中文标题，默认 App 名；`--icon`：`LV_SYMBOL_*`，默认 `LV_SYMBOL_FILE`
- 类名 = 大驼峰（`my_app` → `MyApp`）
- **默认 dry-run**：只打印「要写哪两个文件 + 要在哪 4 处插什么行」，不落盘
- `--apply` 才真写。写文件一律 UTF-8 无 BOM
- 结束打印**屏侧待办清单**（脚本碰不到 .aid）：设计器里建同名屏、屏名 == App 名、按钮事件「调用函数」写 `embark_ui_open_<app>`、跑 `tools/aidogui/export.ps1` 重导出

**② 自动登记 4 处**（旧脚本只登记了 1 处，是"半注册"这种更坏形态）

| # | 文件 | 锚点 | 插什么 |
| --- | --- | --- | --- |
| 1 | `cmake/embark_sources.cmake` | 注释 `# 新 App 登记锚点（加在这里）` 之前 | `        "${EMBARK_ROOT}/app/<名>/<名>_app.cpp"` |
| 2 | `cmake/embark_sources.cmake` | `EMBARK_APP_SOURCES_DEVICE` 列表末 | 同上（真机子集，默认也加上） |
| 3 | `platform/host/ui_demo.cpp` | `EMBARK_APP_TABLE(embark::demo::LauncherApp` 那一行的收尾括号前 | `, embark::demo::<Pascal>App`（**launcher 必须留在第一位**，启动器是默认前台 App） |
| 4 | `app/common/aidogui_ui_hooks.h` | `#define EMBARK_AIDOGUI_HOOK_APPS(X)` 宏体末 | `X(<名>)` |

第 4 处是可选步：不加只是设计器事件里没有这个 App 的"调用函数"入口（纯按钮式 App 可以不挂）。默认加上，并在输出里说明它做了什么。

**③ 不生成的东西**（明确写进脚本输出）

- 不生成 `.aid` / 屏代码 —— 那是设计器的活（AI 只出 HTML 草稿，见 04 号票）
- 不生成 `build/` 里的任何东西
- 不动 `platform/host/ui_tour.cpp`、`platform/host/user_main.cpp`、`platform/esp32/project/main/main.cpp` 的 App 表（那三张表各有用途，不是"所有 App 都该进"）

## 验收

- `node tools/new_app.mjs my_app --dry-run` 打印 2 个文件 + 4 处插入点，**不落盘**（`git status` 干净）
- `node tools/new_app.mjs my_app --apply` 后：`app/my_app/my_app_app.h`（`class MyApp final : public App`、`name()` 返回 `"my_app"`）与 `.cpp` 存在；
  `git diff` 里恰好 4 处登记改动
- 紧接着 `cmake --build build` 通过，且 `embark_host_ui --ui --delay 40` 不回归（新 App 的屏还不存在，
  所以它进不了前台 —— 这一步只验"编得进去、跑得起来"）
- `node tools/new_app.mjs music --apply` 与 `node tools/new_app.mjs my_app --apply`（第二次）都必须**拒绝**
- `node tools/new_app.mjs foo_sub --apply` 必须拒绝（子页保留后缀）
- `node tools/new_app.mjs --self-check` 退出码 0；把 `app/music/music_app.cpp` 里 `ui_backend_tick` 临时改名后重跑必须非 0（自检真的在检查）
- 仓库里 `git grep -n -i "eez\|scaffold_app\.py"` 不因本票新增任何命中

## 备注

- 生成物落盘后**不自动 git add** —— 让人先看一眼 diff。
- 这一票做完之后，`docs/guides/tools.md` §0 的「加 App 没有一键脚本——照 `app/music/` 抄」要改成指向本脚本，
  同时保留「模板只有一份：app/music/」这句（它仍是这条设计的理由）。

---

## 收尾记录（2026-10-18）

### 落地文件

| 文件 | 说明 |
| --- | --- |
| `tools/new_app.mjs`（382 行） | 新建。零依赖 Node ESM，读 `app/music/music_app.{h,cpp}` 当模板 |
| `docs/guides/tools.md` | §0 一览表加 `new_app.mjs` 行；「八个脚本」→「九个脚本」；「加 App 没有一键脚本」整段改写成指向脚本 + `--self-check` |
| `docs/guides/new-app-guide.md` §2 | 「照 `app/music/` 抄」→ 一条命令生成；注册从「两处」改成「5 行 / 3 个文件」；模板唯一真相那段的理由保留并改写成「脚本不内嵌模板」 |
| `app/CMakeLists.txt:18-19` | 注释里的「照 music 抄一份 + 登记」改成 `node tools/new_app.mjs <名> --apply` |

### 与票面的偏离（一处，已落地）

票面写「自动登记 **4 处**」，实际是 **5 行 / 3 个文件** —— 多出来的一行是
`platform/host/ui_demo.cpp` 顶部的 `#include "<名>/<名>_app.h"`。
理由：`EMBARK_APP_TABLE(...)` 引用的是**类名**，头不可见就编不过。
实测第一次 `--apply` 后构建直接失败：

```
error: 'MyApp' is not a member of 'embark::demo'
error: no matching function for call to 'app_registry<..., <expression error> >()'
```

所以第 3 处登记变成两行（include + 表项），其余三处不变：

| # | 文件 | 位置 | 插什么 |
| --- | --- | --- | --- |
| 1 | `cmake/embark_sources.cmake` | `# 新 App 登记锚点（加在这里）` 之前 | `"${EMBARK_ROOT}/app/<名>/<名>_app.cpp"` |
| 2 | `cmake/embark_sources.cmake` | `EMBARK_APP_SOURCES_DEVICE` 里胶水源 `aidogui_ui_nav.cpp` 之前 | 同上 |
| 3 | `platform/host/ui_demo.cpp` | `#include "music/music_app.h"` 之后 | `#include "<名>/<名>_app.h"` |
| 4 | `platform/host/ui_demo.cpp` | App 表收尾括号前（launcher 留第一位） | `, embark::demo::<Pascal>` |
| 5 | `app/common/aidogui_ui_hooks.h` | `EMBARK_AIDOGUI_HOOK_APPS(X)` 宏体末 | `X(<名>)` |

### 实现期三个决定（都不是票面写死的）

1. **类名不叠 App**：模板类叫 `MusicApp`（music + App），若简单拼成 `pascal(name) + "App"`，
   `my_app` 会得到 `MyAppApp`。规则改成「大驼峰后**已经以 App 结尾就不补**」⇒ `my_app` → `MyApp`（与票面期望一致）、
   `clock` → `ClockApp`、`music` → `MusicApp`（与现有四个 App 一致）。
2. **模板的头注释块整个换掉**：`music_app.h` 开头那段写的是「issue launcher-pages 需要第 5、6 个 App 来证明分页」——
   对任何新 App 都是错的。脚本 `stripDocBlock()` 去掉后重写一段（生成日期 + 薄壳纪律 + 后台策略）。
   顺带把正文里两处 music 专属措辞归一：`/// 占位 App：音乐。` → `/// App：<标题>。`、`界面 = AiDoGUI 占位屏` → `界面 = AiDoGUI 屏`。
   这是**唯一**由脚本自带文案的地方；结构、生命周期、日志格式全部来自模板。
3. **`--dry-run` 是默认**，`--apply` 才落盘；落盘后不自动 `git add`（票面备注）。

### 验收实测

| 验收项 | 结果 |
| --- | --- |
| `node tools/new_app.mjs my_app --dry-run` | 打印 2 个文件 + 5 行插入点，`git status` 只有 `?? tools/new_app.mjs`（未落盘） |
| `node tools/new_app.mjs my_app --title 我的应用 --apply` | 写出 `app/my_app/my_app_app.{h,cpp}`（60 / 59 行，UTF-8 无 BOM）；`class MyApp final : public App`、`name()` 返回 `"my_app"` |
| `git diff` | 恰好 5 行登记，分布在 `cmake/embark_sources.cmake`（2）、`platform/host/ui_demo.cpp`（2）、`app/common/aidogui_ui_hooks.h`（1） |
| `cmake --build build` | 通过（第 3 处 include 就是这一步逼出来的） |
| `--ui --frames 40 --delay 40` | exit 0，日志里 `App my_app onExit` 说明它真被注册进表并跑过后台生命周期 |
| 其余四条故事 + tour | 全 exit 0；`--ui-launcher-pages` 从「磁贴 5」变「磁贴 6 / 页 2 / 点 2」仍通过（该断言是下限不是等号）—— **加 App 会改变主屏磁贴，验收日志里的数字会跟着变，别以为是回归** |
| `node tools/new_app.mjs music --apply` | 拒绝：`app/music 已经存在 —— 不覆盖` |
| 第二次 `my_app --apply` | 同上拒绝 |
| `node tools/new_app.mjs foo_sub --apply` | 拒绝：`App 名不能以 _sub 结尾` |
| `node tools/new_app.mjs MyApp --apply` | 拒绝：名字必须小写开头 |
| `node tools/new_app.mjs zz --icon AUDIO --apply` | 拒绝：`--icon 要写成 LV_SYMBOL_XXX` |
| `--self-check` | exit 0 |
| 自检**真的在检查** | 把 `app/music/music_app.cpp` 里 `ui_backend_tick();` 临时改名 → `--self-check` exit 1 且指名「缺「ui_backend_tick();」（泵一帧）」；恢复后 exit 0 |
| `git grep -n -i "eez\|scaffold_app\.py"` | 不因本票新增任何命中 |
| 收尾清理 | 测试用的 `app/my_app/` 与 5 行登记已还原（`git checkout`），工作区只剩脚本与三份文档 |

### 用法（写给下一个人）

```
node tools/new_app.mjs my_app --title 我的应用 --icon LV_SYMBOL_EDIT   # 只看计划（默认 dry-run）
node tools/new_app.mjs my_app --apply                                  # 真写 + 登记 5 行
node tools/new_app.mjs --self-check                                    # 模板与锚点还在吗
```

跑完 `--apply` 会打印「屏侧待办」：设计器里建同名屏、返回按钮写 `embark_ui_open_launcher`、
别的屏要打开它就写 `embark_ui_open_<名>`、重导出、`gen_widget_doc.mjs`、`dev.ps1 -Screen <名>`。

### 没做的（有意）

- 不生成 `.aid` / 屏代码（设计器的活，见 04 号票的工具）
- 不动 `platform/host/ui_tour.cpp`、`platform/host/user_main.cpp`、`platform/esp32/project/main/main.cpp` 三张精简 App 表
- 不自动 `git add`、不自动构建、不自动导出
- CI 不跑 `--self-check`（本仓 CI 不跑任何 Node 脚本），靠改 `app/music/` 的人手工跑一次
