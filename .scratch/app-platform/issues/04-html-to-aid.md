# 04 · HTML→屏 编译链

Status: open
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
