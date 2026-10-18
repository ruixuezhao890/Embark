# 11 · 一键生成 App（AiDoGUI 版脚手架）

Status: open
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
