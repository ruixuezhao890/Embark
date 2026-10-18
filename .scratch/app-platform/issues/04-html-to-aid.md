# 04 · HTML→屏 编译链

Status: done（2026-10-18）
Blocked by: 02, 03

## 为什么

Q5/Q6/Q13/Q14。用户诉求（m00075）："画屏是想通过 **HTML 让 ai 帮我画屏**，然后把要交互数据的接口和地方给我一个文档"。

分工：**AI 只写语义**（这是标题、这是卡片、这是返回按钮），**坐标由浏览器算**。这样 AI 的输出从"每个控件 6 个数字 + 完整 style 对象"降到"一段 HTML"。

## 三条硬事实

**① `.aid` 的属性面是可穷举的**

现有 6 屏 / 124 控件用到的 style 属性**只有 14 个**：`textFont` / `textColor` / `textFontSize` / `textAlign` / `bgColor` / `radius` / `padLeft` / `padRight` / `padTop` / `padBottom` / `bgOpa` / `borderWidth`（`DEFAULT__MAIN`）+ `bgColor`（`PRESSED__MAIN`）。

widget 顶层键恰好 14 个：`id / parentId / type / name / x / y / width / height / text / style / events / flags / state / scrolling / align`。类型只有 4 种：`lv_obj` / `lv_label` / `lv_bar` / `lv_calendar`。颜色统一 `#rrggbbaa`。

⇒ **不需要通用 CSS 引擎**，只需覆盖这 14 项 + 4 种元素。

**② 渲染器用 Edge 无头**

本机已有 `msedge.exe`，零新增依赖、零下载，Chromium 内核的 `getBoundingClientRect()` 语义正确。用 `--headless --dump-dom` 配合注入脚本输出 JSON。

**③ 交互只写 `data-action`，不写 JS**

`<button data-action="open_weather">天气</button>` ⇒ .aid 里生成：

`{"id":"event_<widget>_clicked","eventType":"CLICKED","actionType":"CALL_FUNCTION","actionParams":{"functionName":"embark_ui_open_weather"},"enabled":true}`

App 里绑 `aidogui_ui_bridge_action("open_weather")`。这是**命名锚点，不是脚本**。

## 交付物

`tools/aidogui/html_to_aid.mjs`（Node、零依赖，照 `tools/font/audit_glyphs.mjs` 的风格）：

`node tools/aidogui/html_to_aid.mjs --html draft/weather.html --screen weather --out app/aidogui_ui/screen.aid`

流程：读 HTML → 起 Edge 无头量每个元素的 `getBoundingClientRect()` 与 `getComputedStyle()` → 映射到 14 项 style → 生成 widget 数组 → **合并进现有 .aid 的对应 screen**（不重写整个文件，保住其他 5 屏）。

约束：

- **不覆盖已有控件名**（重名直接报错退出，照 `.scratch/retitle_font_row.cjs` 的"出现次数必须为 1"风格）
- 只写 `screens[<screen>].widgets`，其余顶层键原样保留
- 输出前自检：widget 数 > 0、每个 `id` 唯一、每个 `parentId` 存在或为屏根

## 验收

- 用现有 `weather` 屏反推一份 HTML 草稿，跑工具 → 生成的 .aid 与现有 `screen.aid` 的 weather 屏**逐控件对得上**（x/y/width/height/text/style 全等）
- 生成后跑 `pwsh tools/aidogui/export.ps1` → `cmake --build build` → `embark_host_ui --ui-weather --delay 40` 不回归
- 故意写一个重名控件 → 工具报错且**不落盘**

---

## 收尾记录（2026-10-18）

### 落地的文件

| 文件 | 行数 | 作用 |
| --- | --- | --- |
| `tools/aidogui/html_to_aid.mjs` | 808 | 主工具（零依赖 Node ESM）：三个子命令 + 映射 + 合并 + 自检 |
| `tools/aidogui/aid_probe.js` | 101 | 注入草稿里跑的**浏览器侧探针**：收集每个 `[data-name]` 的 rect 与计算样式，塞进 `<script id="embark-probe">` 供 `--dump-dom` 取回。单独成文件是因为写在 `.mjs` 里当字符串会被转义搅坏 |
| `docs/guides/html-to-screen.md` | 122 | 草稿方言、CSS 映射表、安全网、自检、踩过的坑 |

改动：`docs/guides/tools.md`（§0 加两行 + 「六个脚本」）、`docs/index.md`（guides 表加一行）。

### CLI（三个子命令，实测都通）

```sh
node tools/aidogui/html_to_aid.mjs --html <文件> --screen <屏名> [--apply] [--allow-drop]
node tools/aidogui/html_to_aid.mjs --from-aid <屏名> --out <文件.html>
node tools/aidogui/html_to_aid.mjs --self-check [--screen <屏名>]
```

`--aid <文件>` 默认 `app/aidogui_ui/screen.aid`；`--browser <exe>` / `EMBARK_BROWSER` 可换浏览器（默认找 msedge / chrome）。

### 与票面的三处偏离（都是做的时候发现必须的）

1. **多了 `--from-aid`（反推草稿）**。票面只要求单向（HTML → 屏）。但没有反推就没法改**现有**屏 —— 「现有屏 → 草稿 → 改 → 量回来」是主要用法，也是往返自检的手段。所以双向做进同一个 CLI。
2. **写回不是「整屏替换」，而是「逐字段合并」**。票面只写「不覆盖已有控件名」。实测整屏重生成会让 37 个控件的键序与显式 `0` 全抖一遍（31 插入 / 83 删除 的纯噪音 diff）。改成：没变的控件**原对象原样保留**，变了的只覆盖真变了的字段（`box` / `text` / `parentId` / `type` / `clickable` / `scrollable` / `action` / `style` 逐键）。结果：**无改动往返字节完全一致**（`md5` 不变、`git diff` 空），单字段改动只出一行 diff。
3. **自检做成 `--self-check` 子命令，不是独立测试文件**。它必须起真浏览器量真像素，放进 `tests/` 会让 ctest 依赖 Edge。

### 实现期踩的四个坑（映射表就是这么定下来的）

1. **颜色**：`getComputedStyle` 只给 `rgb()` / `rgba()`，`.aid` 要 `#rrggbbaa` ⇒ 自己解析；反过来 `data-pressed-bg` 是 `#rrggbbaa` 文本，也得能解析（只认 `rgb()` 会报「不是颜色」）。
2. **`clickable` 不能只看有没有 events**：weather 屏的 `Btn_CityPrev` / `Btn_CityNext` 的点击动作绑在 C 里（`aidogui_ui_nav_bind_click`），`.aid` 里 `flags.clickable = true` 但 `events = []`；按 events 推会把它降级成不可点容器。⇒ 加 `data-clickable` 显式标记，往返才不丢。
3. **`textFont` / `textColor` 不是 label 专属**：clock 的 `lv_calendar`（`Cal`）同样带 `textFont/textColor/textFontSize`。按「非 lv_obj 就写文字属性」会让 `lv_bar`（`Bar_Prog`）被硬塞一套没用字体。⇒ 规则改成：`lv_label` 一律写；其它类型只在草稿标了 `data-text` 或元素真装了文字时才写。
4. **空格与连字符**：`ALIGN_BY_CSS` 里 `justify-all` / `match-parent` 这类带连字符的键必须加引号，否则整个 `.mjs` 语法错（`SyntaxError: Unexpected token '-'`）。

### 四处语义归一（不比原始 JSON，比归一化形式）

- `bgOpa = 0` ≡ 没有背景色（`#00000000` 与缺键在屏幕上完全一样）
- `textAlign` 缺省 = `LEFT`
- 数值缺键 = `0`（`.aid` 里 `padLeft:0` 有的控件写、有的不写）
- `bgColor` 只比 RGB 六位，alpha 归 `bgOpa`

### 验收实测

| 票面验收 | 结果 |
| --- | --- |
| weather 屏反推草稿 → 跑工具 → 逐控件对得上 | `--self-check --screen weather` → `〔自检通过〕`，37 个控件 `box/text/style 14 项/clickable/scrollable/action` **逐字段全等**。另外五屏也全过：launcher 14 / clock 27 / music 7 / album 7 / settings 32 |
| 生成后 export → build → `--ui-weather --delay 40` 不回归 | 实跑：草稿把 `Btn_Back` 的 `left` 从 8 改 12 → `--apply`（diff **只有一行** `"x": 8 → 12`）→ `export.ps1`（`ui_weather.c` 出 `lv_obj_set_x(ui_weather_Btn_Back, 12)`）→ `cmake -S . -B build` + `cmake --build build` 通过 → `--ui-weather --delay 40` **exit 0**、`--ui --frames 40` exit 0。之后已 `git checkout` 复原 |
| 重名控件 → 报错且不落盘 | 把草稿里第二个 `data-name` 改成 `Desk` → **exit 1**（`错误：控件名重复：Desk…`），`screen.aid` 的 md5 前后不变、`git diff` 空 |

顺带验证：**无改动往返字节一致** —— `--from-aid weather` → `--apply`，`md5` 不变（`d73be248…`），`git diff --stat` 空。

### 没做（有意）

- **不新建屏**：只合并进 `.aid` 里已存在的屏。新屏还是在设计器里建（或用 `.aid` 手工加一条 `screens[]`）。
- **不碰** `bitmaps[]` / `variables` / `lvglAnimations` / `i18n` / `fonts[]` / `settings`：整文件重序列化时这些区原样带过（`.aid` 的字节格式恰是 `JSON.stringify(obj, null, 2) + "\n"`，已实测）。
- **不做通用 CSS 引擎**：只读 10 个属性（映射到 14 个 `.aid` 键），其余 CSS 只当布局手段 —— 量的是最终 rect。
- **不处理图片**：草稿里放 `<img>` 不会变成 `.aid` 的 bitmap。
