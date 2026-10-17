# 06 审计脚本换数据源 + 文档同步

Status: done（2026-10-17 落地；CI 待推上去跑一次）
Stage: 后删
Blocked by: 03

## 做什么

- `tools/font/audit_glyphs.mjs`：数据源从 `tools/font/embark_zh_14.json`（子集清单）换成 TTF 文件的 cmap。
  ⚠️ 19 个 setter 的白名单必须同批复核 —— 只换数据源不清白名单，会把"没扫到"误报成全绿。
  `:44` 的图标区豁免（U+F000–U+F8FF）必须保留，`--selftest` 与 `--print-required` 语义跟着调。
- 文档同步：`GLOSSARY.md` 相关条目、`app/aidogui_ui/README.md`、`docs/guides/scaffold-and-tools.md`、
  `docs/reference/pitfalls.md`、`docs/adr/0011-aidogui-ui-source.md`、`.github/workflows/ci.yml`、
  `tools/font/gen_font.mjs` 头部，以及指向 BIN/清单的过时说法。

## 验收

- 故意在源码里埋一个 TTF 没有的码点（如 U+AC00 谚文），CI 的 font-audit job 必须红。
- 文档里搜 `BIN 字体` / `拷卡` 只剩历史叙述。

## 落地（2026-10-17）

### 脚本：对照 TTF 的 cmap，零依赖

- 默认字体 `app/aidogui_ui/assets/font/ui_heiti.ttf`（= 卡上 `fonts/` 那份的源头，git 跟踪，CI 里必在）；
  新增 `--ttf <path>` 换目标、`--roots=app,src` 改扫描根、`--include-symbols` 关掉图标豁免、`--help`。
- 自己解 cmap：校验 sfnt 版本（`\0\1\0\0` / `OTTO` / `true` / `ttcf`），遍历子表取 platform 0 与
  platform 3，支持 **format 4**（BMP，`idRangeOffset` 非 0 时按字节偏移取字形号，字形号 0 视为无字）
  与 **format 12**（分组）。实测 `ui_heiti.ttf` 只有两个 format 4 子表 ⇒ **没有 BMP 外字符**，
  7582 个码点（与 ADR 0017 引用的数字一致）。
- 白名单从 19 条改为 18 条：删掉 `lv_msgbox_add_text` / `_add_title` / `_add_close_button`
  （这三个 API 在 8.3.11 里不存在，8.3 的 msgbox 是 `lv_msgbox_create(parent, title, txt, btn_txts[], add_close_btn)`，
  见 `third_party/lvgl/src/extra/widgets/msgbox/lv_msgbox.h:63`），补 `lv_msgbox_create` 与 `lv_dropdown_add_option`。
- 新增白名单守卫 `SUSPECT_PATTERNS` + `unknownSettersOf()`：扫到名字像 setter（`set_text*`、`text_static`、
  `set_options`、`add_option(s)`、`add_btn`、`add_tab`、`set_map`、`set_cell_value`、`add_title`、`add_text`、
  `set_placeholder`、`msgbox_create`）却不在白名单里的调用就报告 + exit 1 —— 只换数据源不清白名单会把
  "没扫到"伪装成全绿，这条就是治它的。`/set_text/` 带边界才不会误伤 `lv_keyboard_set_textarea`。
- 字面量解码改为**先还原字节、再整体按 UTF-8 解码**：`\xNN` 推字节、`\uXXXX` 推其 UTF-8、八进制 `\NNN`、
  命名转义、裸字符按 UTF-8 编码；非 BMP 字面量整码点吞掉（否则被拆成两个孤立代理、编成 `EF BF BD`）。
  过滤 `cp < 0x20` / `0x7f` / `0xfffd`。
- 输出：字体行打 cmap 码点数；豁免行打「豁免 N 个内置图标（U+F000–U+F8FF）」；缺字按码点排序打
  「U+XXXX 「字」 — 文件:行号」（每码点最多 3 处）+ 三条修法（换字体 / 改文案 / 补 `textFont`）。
  `--print-required` 打去重字符数与全部字符，`--json` 增加 `font{file,codepoints}` 与 `unknownSetters`，
  `--ttf` 指的文件不存在时 exit 2。

### 验收证据（全部真跑）

- `node tools/font/audit_glyphs.mjs --selftest` → **自检 PASS**：龘 U+9F98 与谱号 U+1D11E 都报缺、行号 2、
  全命中夹具「启」不误报、U+F0B9 默认豁免而 `--include-symbols` 报缺、`lv_msgbox_add_title` 被白名单守卫抓到。
- `node tools/font/audit_glyphs.mjs` → 扫 `app/` `src/` `platform/` **151 个源文件、400 个字符引用、去重 139 个**；
  豁免 10 个图标（U+F001 F008 F013 F03E F043 F054 F0C9 F0E0 F124 F304）；`✓ 界面文案全部落在字体的 cmap 里`，exit 0。
- 故意埋字：`app/_audit_probe.c` 写 `"\xEA\xB0\x80"`（UTF-8 三字节的「가」）→ 报 `U+AC00 「가」 — app/_audit_probe.c:2`、
  exit 1；写裸 UTF-8「汉字都认识한글」→ 报 U+AE00「글」与 U+D55C「한」、exit 1。探针文件用完已删。
- `--print-required`：139 个不同字符（剔除 10 图标）、缺 0 个；`--json` 首部 `{"font":{"file":…,"codepoints":7582},…}`。

### 两个被自己抓到的解码 bug（记下来免得重犯）

1. `\xEA\xB0\x80` 被当成三个 Latin-1 字符（U+00EA U+00B0 U+0080）⇒ 谚文「가」在 C 里正是这么写的，
   却只报了个控制符。修法就是上面那条：按字节流还原再整体 UTF-8 解。
2. 非 BMP 字面量（U+1D11E）按 UTF-16 码元逐个 `Buffer.from(c)` ⇒ 两个孤立代理各自被编成 `EF BF BD`，
   自检里的"谱号"因此漏报。修法：`codePointAt(i) > 0xffff` 时整码点吞掉、`i += 1`。

### 文档同步（都已改）

- `.github/workflows/ci.yml:7-9`（job 说明改成对照 TTF cmap）、`:81` job 名去掉 issue 编号、`:90` 步骤名 `against font cmap`。
- `app/aidogui_ui/README.md` 字体小节：审计「今天查的还是内置 embark_zh_14 的清单」改成「查的就是这份 TTF 的 cmap」。
- `docs/guides/scaffold-and-tools.md:152`（清单标注为存档）、`:187-203` 整节重写（四个用法、对照物、`UI_TEXT_SETTERS` 18 条、
  `SUSPECT_PATTERNS` 守卫、缺字三条出路）。
- `docs/reference/pitfalls.md`「LVGL 界面文案」整段重写：不再说「补 UI_TEXT 重新生成字库」，改成审计对照 cmap + 三条出路。
- `GLOSSARY.md:242` Coverage audit 改成已落地口径（TTF cmap、18 个 setter、`SUSPECT_PATTERNS`、五个开关）；
  `:207` Static subset font 里「清单还留着」改成「只是存档，审计不再读它」。
- `docs/adr/0011-aidogui-ui-source.md` 加**状态**行：第 4/5 条的字体 BIN 部分已被 ADR 0017 取代，图片 BIN 不变。
- `tools/font/gen_font.mjs:7` 与 `:173` 两处注释同步为「清单=存档」。
- 不改（历史 ADR 正文）：`docs/adr/0007-font-management.md:26,43`、`docs/adr/0008-eez-studio-adapter.md:52,68`、
  `.scratch/embark-v1/issues/17-*.md`。

### 还没做

- **CI 没真跑过**（本地是同一脚本、同一份 TTF，`--selftest` + 全量审计都绿；推上去才算数）。
- `docs/reference/limits.md` 本来就不提审计数据源，复核后无需改动；`platform/esp32/README.md:141-147` 早就是 TTF 口径。
- 审计不查 `lv_keyboard_set_textarea`（仓库里 3 处，但输入法文案不上屏）。
- `--json` 不带子表明细（只在人读的报告里说 cmap 码点总数）。
