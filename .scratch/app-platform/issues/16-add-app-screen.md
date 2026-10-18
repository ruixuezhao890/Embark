# 16 加 App 时顺手把屏也建出来（占位屏克隆 + 反向撤）

Status: done（2026-10-18）· Blocked by: —（11/15 票已落地）

## 为什么

11 票把「加一个 App」压成一条命令，但它只管 C++ 薄壳。用户 m04468 问「新建的 app，页面怎么使用 html 创建？」
时暴露的断链是：**`html_to_aid.mjs` 只合并进已存在的屏，不新建屏**（`tools/aidogui/html_to_aid.mjs:127`
`requireScreen` 直接 fail「屏 X 不在 .aid 里」）。于是新 App 的标准流程是：先在设计器里手工克隆一个占位屏 →
导出 → 才轮到 HTML 草稿。这一步纯手工、每次都要做，而且漏了就是 `--screen my_app` 一屏黑 + 桥一句 warn。

关键未知（决定这条能不能做）：**设计器能不能正常导出「脚本拼出来的屏」**（不是它自己 UI 建的）。先验证，再落。

## 做什么

| # | 能力 | 落点 |
| --- | --- | --- |
| ① | `--apply` 顺带往 `.aid` 里插一张同名占位屏 | `tools/new_app.mjs` 的 `buildPlaceholderScreen()` / `planScreen()` |
| ② | 占位屏长什么样：抄 `album`（仓库里那块「界面还没做」的屏）的 7 个控件 | `PLACEHOLDER_TEMPLATE_SCREEN = 'album'`，深拷贝 + 改 `id`/`parentId`/`events[].id` 前缀 |
| ③ | 选哪份 `.aid` | `--aid <文件>` > 开发台记住的 `tools/devpanel/.state.json` > `app/aidogui_ui/screen.aid` |
| ④ | `--delete` 反向撤屏，且**只在没被改过时**撤 | `planScreenRemoval()`：屏空 / 控件名序列与生成的占位屏一致 → 删；否则 `action:'keep'` + 「看着像你改过了，我不动它」 |
| ⑤ | `--self-check` 兜住这套 | 模板 `album` 还在 + 返回键仍绑 `embark_ui_open_launcher` + `serializeAid()` 原样写回**字节一致** |
| ⑥ | 面板两个按钮也走同一条路 | `tools/devpanel/server.mjs`：`--apply` 之后**先 `export.ps1 -Project <当前 .aid>` 再构建** |

## 验收

- `node tools/new_app.mjs --self-check` exit 0。
- dry-run 打印「要加一张屏 X：抄 album 那块占位屏的 7 个控件」，`git status` 无变化。
- `--apply` 到一份**副本 .aid** 后：新屏 7 个控件 id 前缀正确、返回键事件是 `embark_ui_open_launcher`。
- `export.ps1 -Project <副本>` → `filesGenerated` 正常、`warnings: []`、生成 `ui_<app>.c`（7 个控件、
  `screen_init/destroy` 齐全）、接进 `export/ui.c` 与 `export/ui.h`。
- 构建后 `--screen <app> --frames 30`：`屏 <app> 已装载（7 个控件）`，exit 0。
- `--delete --apply` 往返后 `cmake/embark_sources.cmake` 与 `aidogui_ui_hooks.h` 零残留 diff。
- 全量：build / ctest 2/2 / 六条验收故事 / `check_docs` / `gen_widget_doc --check` 全绿。

## 没做（有意）

- 不生成屏的 C 代码 —— 那是设计器导出的产物；脚本只改 `.aid`，流程里必须还有一次 `export.ps1`。
- 不动 `.aid` 的 `fonts[]` / `bitmaps[]` / `variables[]`。
- 不给占位屏配动画、多语言、事件（除了返回键）。
- `export.ps1` 仍只认默认 `-OutDir`（验证时只能导到真目录再 `git checkout` 复原）。
