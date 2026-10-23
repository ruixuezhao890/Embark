# 用 HTML 草稿画屏（`html_to_aid.mjs`）

> **这一层回答「怎么不打开设计器、只写一段 HTML 就把屏改出来」**。
> 界面代码从哪来 → [../../app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)；工具全貌 → [tools.md](tools.md)。
> **要让 AI 批量写草稿** → §1 有硬约束、一条可直接贴的 prompt，和一份能跑通的模板 [tools/aidogui/draft-template.html](../../tools/aidogui/draft-template.html)。

---

## 0. 一句话

**坐标不该由人或 AI 猜 —— 让浏览器算。** 你（或 AI）只写「这是一个标题 / 这是一张卡片 / 这个按钮点了进天气」这类语义，
`getBoundingClientRect()` 给出真几何，工具把它翻成 `.aid` 的控件表，再走原来的导出链路编译。

```sh
# ① 现有屏 → 草稿（快照，每个元素都绝对定位到它在 .aid 里的坐标）
node tools/aidogui/html_to_aid.mjs --from-aid weather --out .scratch/draft/weather.html

# ② 改草稿（改 left/top，或者整个重写成 flex —— 量的是最终 rect）

# ③ 量回去：默认只打印差异，加 --apply 才写盘
node tools/aidogui/html_to_aid.mjs --html .scratch/draft/weather.html --screen weather --apply

# ④ 重生成 C 代码 → 构建 → 跑验收故事
pwsh -NoProfile -File tools/aidogui/export.ps1
cmake -S . -B build && cmake --build build
./build/platform/host/embark_host_ui --ui-weather --delay 40
```

**`.aid` 仍然是唯一真相**：草稿是草稿，随时可以丢；设计器照样能打开 `screen.aid` 手改。

---

## 1. 让 AI 画草稿：硬约束 + 一条 prompt

**这一节回答「让 AI 产草稿时哪几条不能错」**：每条都配它违反时的**真报错**，好让你一眼看出是草稿的毛病、不是工具的。
方言的权威表在 §2 / §3，本节只收「生成时最容易踩空」的那些。

| 硬约束 | 违反了会怎样 |
| --- | --- |
| 每个要上屏的元素都写 `data-name`（字母开头 + 字母数字下划线、同屏唯一） | `错误：一个 [data-name] 都没量到：草稿里的控件要用 data-name 命名`，退出码 1。没名字的元素不会被量、不会上屏。 |
| 只用三类标签：容器（`div` / `section` / `article` / `ul` / `li` / …）、文字（`span` / `p` / `h1`–`h6` / `b` / `label` / …）、进度条（`progress`） | `认不出 <button> 是哪类控件：给它加 data-type …`。`button` / `a` / `img` / `table` 要显式写 `data-type`，干脆别用更省事。 |
| 文字写在带 `data-name` 元素的**直接文本节点**里 | 带 `data-name` 的 `span` 里再套一个 `b` 包住文字，量到的是**空文本** —— 字被包一层就没了。 |
| 要显示字就用 label 类标签，或显式 `data-type="lv_label"` | 在 `div` 里写字，上屏是一个**空容器**：只剩字号和颜色，文字内容丢失。 |
| 每个元素都有真实盒子（宽高 > 0） | `控件 X 量到的尺寸是 0x0：元素没占空间` —— 空元素不给宽高就是这样。 |
| 文字控件显式写 `font-size` / `text-align` / 宽高 | 字号量不到就没有 `textFontSize`，渲染按默认值走，字号不可控。 |
| 单文件、全内联：没有 `<script>`、没有外部 CSS / JS / 字体 / 图片 | 量的是静态 DOM；图片工具根本不搬（它不碰 `bitmaps[]`），上了屏也没有。 |

**不许用**：emoji、图标字体、`LV_SYMBOL` 字形 —— 画布只有 `ui_heiti` 这一套字体，它们会显示成方块。

**交付前自检 6 条**（让 AI 自己先过一遍，比事后返工省事）：

1. 每个上屏元素都有 `data-name`？有没有重名？
2. 文字有没有被 `b` / `span` 之类又包了一层，导致父元素自己没有直接文本？
3. 有没有用 `div` 装文字（会丢字）？
4. 每个文字控件都有 `font-size` / `text-align` / 宽高？
5. 有没有 `0x0` 的空盒子、有没有控件跑到 240×320 外面？
6. 有没有 `<script>` / 外部资源 / emoji，或非白名单标签漏写 `data-type`？

### 1.1 可直接贴的 prompt

把 `{{…}}` 换成这次要画的内容。**方言一变（§2 / §3）就要跟着改这一处** —— `check_docs` 查不出 prompt 里的过期说法，靠人守。

```text
# 角色
你在为 Embark（LVGL 嵌入式界面）生成「HTML 草稿」。草稿不是网页，是一份给工具的输入：
工具用无头浏览器量每个元素的像素，再翻成 LVGL 控件。

# 只输出
一个 .html 文件的完整内容。不要解释，不要 Markdown 代码围栏。

# 画布（固定）
- 240×320 竖屏；<body> 按这个尺寸排版，margin:0。
- 单文件、全内联：一个 <style>，没有 <script>，没有外部 CSS / JS / 字体 / 图片。
- 字体统一 font-family: ui_heiti（画布上只有这一套）。颜色只用 #rgb / #rrggbb / #rrggbbaa / rgb() / rgba()。

# 控件 = 带 data-name 的元素（硬门槛）
- 每个要上屏的元素都写 data-name="C标识符"：字母开头，只含字母/数字/下划线，同屏唯一。
  没写 data-name 的元素不会被量、不会上屏（工具会直接报「一个 data-name 都没量到」）。
- 只用这些标签：
  · 容器（→lv_obj）：div section article header footer nav main aside ul li figure
  · 文字（→lv_label）：span p h1 h2 h3 h4 h5 h6 b strong em small i label
  · 进度条（→lv_bar）：progress
- 别的标签（button a img table input …）必须写 data-type，取值 lv_obj / lv_label / lv_bar / lv_calendar，
  否则报「认不出这个标签是哪类控件」。建议干脆别用。
- 点了要跳 App：data-action="open_weather"（工具自动补 embark_ui_ 前缀）。
  目前只有 open_launcher / open_clock / open_settings / open_weather。
- 可点但动作绑在 C 里：data-clickable。可滚动容器：data-scroll="x|y|both"。
- 进度条/日历必须显式 data-type；日历自己要有文字样式时再加 data-text。

# 文字（最容易白干的两条）
- 文字必须是带 data-name 元素的「直接文本节点」：
  <span data-name="t">你好</span> 可以；把「你好」再包进 b 就量到空文本。
- 只有 lv_label 会真的把文字带走：在 div 里写字，上屏是一个空容器（只剩字号和颜色）。
  要显示字就用 span/p/h1..h6，或给元素写 data-type="lv_label"。
- 每个文字控件都显式写 font-size（px）、text-align（left|center|right）、width、height。

# 盒与样式（工具只读这 10 个属性）
background-color / color / font-size / font-family / text-align /
border-radius / border-width / padding / width / height
- 其余 CSS（flex、grid、margin、gap、transform）随便用，量的是最终矩形；
  但每个元素都必须有真实盒子：空元素不给宽高会量成 0x0 直接报错。
- 边框四边写一样（工具只取上边框宽度），例如 border: 1px solid transparent。
- 底色用 rgba 时 alpha 会被单独记成透明度；alpha 为 0 等于没有底色。

# 结构
- 父子关系靠 DOM 嵌套：内层元素写在祖先里面，坐标自动变成「相对父控件内容区」的偏移；
  父元素必须在文档顺序里先出现。
- 要精确控制位置就用 position:absolute + left/top/width/height；想省事就用 flex，工具量最终位置。

# 不许用
图片 / SVG 位图（工具不搬 bitmaps，上了屏也没有）、emoji / 图标字体 / LV_SYMBOL 字形
（画布只有中文字体，会显示成方块）、<script>、外部资源、动画、新建屏（只能画进已存在的屏）。

# 交付前自检（逐条过，别省）
1. 每个上屏元素都有 data-name 吗？有没有重名？
2. 有没有把文字又包进一层标签，导致父元素自己没有直接文本？
3. 有没有用 div 装文字（会丢字）？
4. 每个文字控件都有 font-size / text-align / 宽高？
5. 有没有空盒子（0x0）、有没有元素跑到 240×320 外面？
6. 用了 button/a/img 之类却没写 data-type 吗？有 <script> / 外部资源 / emoji 吗？

# 这次要画的
屏名：{{屏名}}
内容：{{逐条写清楚：有几个区块、每块显示什么文字或数值、点击后去哪、要不要滚动}}
配色和字号没指定就自己按 240×320 的密度定（正文字号 12–14px 最稳）。

只输出 HTML。
```

### 1.2 一份能跑通的样例

[draft-template.html](../../tools/aidogui/draft-template.html) 是 240×320 的最小骨架：文字 / 卡片 / 滚动区 / 进度条 / 跳转按钮各一个。两个用法：

```sh
# ① 验证工具链还活着（默认 dry-run，一个字节都不写）：量出 11 个控件 = 浏览器 + 探针 + 映射表都对
node tools/aidogui/html_to_aid.mjs --html tools/aidogui/draft-template.html --screen settings --allow-drop

# ② 抄片段：先拿真屏的草稿当底，再把模板里的某块贴进去
node tools/aidogui/html_to_aid.mjs --from-aid weather --out .scratch/draft/weather.html
```

① 那条里的 `--allow-drop` 只是为了「11 个控件 vs 原屏 8 个」的差异不把退出码拉成 1（dry-run 不写盘）；
实测输出是「草稿量出 11 个控件（原屏 8 个）」，再跑一次同样的 `--apply` 会得到「保持不变 11」，即模板本身往返零 diff。

模板的控件比真屏少，直接 `--apply` 合并会被「消失」拦下来（见 §4）：正常干活是**以 `--from-aid` 的草稿为底**、只贴需要的片段，而不是拿模板整体覆盖。

> 草稿存哪儿由你定：`html_to_aid.mjs` 只读你给的那个文件（建议放 `.scratch/draft/`，已在 `.gitignore` 里）—— 想留档就别删它。

---

## 2. 草稿方言：只用 `data-*` 表达语义

画布尺寸取 `.aid` 的 `settings.width` / `settings.height`（今天 240×320），草稿里 `body` 会被强制成这个尺寸、`margin:0`。

| 属性 | 必填 | 作用 |
| --- | --- | --- |
| `data-name` | ✅ | **控件名**，必须是同屏唯一的 C 标识符（字母开头 + 字母数字下划线）。只有带 `data-name` 的元素会被量；名字就是屏里控件的 `name` 与 `.aid` 里的 `id` 后缀。 |
| `data-type` | 按需 | `lv_obj` / `lv_label` / `lv_bar` / `lv_calendar`。省略时 `<span>` → `lv_label`，其它 → `lv_obj`。**`lv_bar` / `lv_calendar` 必须显式写**（量不出它是不是进度条）。 |
| `data-action` | 按需 | 点击后调的 C 函数：`open_weather` 或写全 `embark_ui_open_weather`（没前缀就自动补）。生成 `CLICKED` + `CALL_FUNCTION` 事件，函数名必须出现在 `app/common/aidogui_ui_hooks.h` 的 X-macro 表里才能编译过。 |
| `data-pressed-bg` | 按需 | 按下态背景色 `#rrggbbaa` → `PRESSED__MAIN.bgColor`（按钮的按压反馈）。 |
| `data-clickable` | 按需 | 可点、但动作绑在 C 里（`aidogui_ui_nav_bind_click`）的控件要写。否则它会被当成不可点的普通容器。 |
| `data-scroll` | 按需 | `x` / `y` / `both` → `flags.scrollable` + `scrolling.scroll_dir`。 |
| `data-text` | 按需 | 「不是 label、但自己有文字层」的控件（`lv_calendar` 自己画字）要写，否则它的 `textFont` / `textColor` 会丢。 |
| `data-font` | 多字体时 | 选 `.aid` 的 `fonts[]` 里的哪一套。今天只有 `ui_heiti.ttf` 一套，可省略。 |

**父子关系靠 DOM 嵌套**，不靠坐标：内层元素写在祖先里面，它的 `x/y` 自动等于「相对父控件内容区」的偏移
（LVGL 子控件原点是父控件边框 + 内边距之后，工具已经把这层减掉了）。父元素必须在文档顺序里先出现。

---

## 3. CSS 里哪些算数

只有这 10 个属性会被读走（`.aid` 的 style 面本来就只有 14 个键，其余没有对应物）：

| CSS | `.aid` 字段 | 规则 |
| --- | --- | --- |
| `background-color` | `bgColor` + `bgOpa` | `bgColor` 只存 RGB，透明度一律走 `bgOpa`（`alpha × 255` 取整）。**alpha 0 = 没有背景**，与缺键等价。 |
| `color` | `textColor` | 保留 alpha。 |
| `font-size` | `textFontSize` | 必须量得到（px）。 |
| `font-family` | `textFont` | 与 `.aid` 的 `fonts[].name` 比对（去扩展名）。 |
| `text-align` | `textAlign` | `left`/`start` → `LEFT`，`center` → `CENTER`，`right`/`end` → `RIGHT`。 |
| `border-radius` | `radius` | 大于 0 才写。 |
| `border-width` | `borderWidth` | 大于 0 才写。 |
| `padding` | `padLeft/Top/Right/Bottom` | 任一方向大于 0 才写（四个一起写）。 |
| `width` / `height` | `width` / `height` | 量到的盒尺寸，**必须大于 0**。 |

**其它的 CSS 随便用**：flex / grid / margin / gap / transform 都不影响正确性 —— 工具量的是最终 rect，不是为了读懂你的布局。
但要注意：宽高必须来自元素实际占据的盒子（`width:auto` 配空内容量出 0 会直接报错，那就给它一个尺寸）。

文字宽度是**真实 TTF** 量出来的：工具按 `.aid` 的 `fonts[].sourcePath` 注入 `@font-face` 等 `document.fonts.ready` 之后再量
（14px 两个汉字 = 28×15，回退字体则是 28×20）。字体文件不在时打警告并退回系统字体 —— 这种时候给文字控件写死宽高最稳。

---

## 4. 安全网：默认不写盘、坏草稿不落盘

| 情况 | 行为 |
| --- | --- |
| 不给 `--apply` | 只打印差异：`保持不变 / 有变化 / 新增 / 消失` + 每个变化控件的字段级 diff，**一个字节都不写** |
| 控件重名 | 报错退出（`退出码 1`），不写盘。重名的第二个元素会被顶掉，工具不去猜你要哪个 |
| 父元素不在前面 / 名字不是合法标识符 / 尺寸 0 / 尺寸 > 4096 | 同上，报错退出、不写盘 |
| 新草稿比原屏少控件 | 打印「消失」清单并报错；确认要删就加 `--allow-drop` |
| 控件比原来多 | 追加在屏末尾（DOM 前序顺序），不需要额外开关 |

**没变的控件原样保留**（连键序和 `.aid` 里显式写的 `0` 都不动），只有真变了的字段被覆盖。
后果：草稿没改任何东西跑一趟，`screen.aid` **字节完全一致**（`git diff` 是空的）；改一个 `left` 就只出一行 diff。

改动范围也锁死：**只写 `screens[<屏名>].widgets`**，其它 5 屏、`fonts[]`、`settings`、`bitmaps[]` 一个字都不碰。

---

## 5. 自检：`--self-check`

```sh
node tools/aidogui/html_to_aid.mjs --self-check                  # 默认 weather
node tools/aidogui/html_to_aid.mjs --self-check --screen clock   # 换一屏
```

它跑两件事：**① 重名草稿必须被拒**（纯函数，不启浏览器）；**② 往返一致**：把该屏的 `.aid` 反推成草稿 →
起无头浏览器量 → 翻回控件，与原控件**逐字段全等**（`box` / `text` / `style` 14 项 / `clickable` / `scrollable` / `action`）。
末行打 `〔自检通过〕` 或 `〔自检失败：N 项〕`，退出码 `0` / `1`。

六屏实测全过（launcher 14 / clock 27 / weather 37 / music 7 / album 7 / settings 32 个控件）。
这条自检是**改工具之后必须重跑的**：它一红就说明映射表被改坏了，别拿去改真屏。

---

## 6. 环境与代价（踩过的）

- **无头浏览器**：默认找 `msedge.exe`（`C:/Program Files (x86)/Microsoft/Edge/Application/`）与 chrome；可用 `--browser <exe>` 或环境变量 `EMBARK_BROWSER` 覆盖。启动参数里有 `--allow-file-access-from-files`：`@font-face` 要读本地 TTF 文件，缺这个开关字体加载会静默失败，量出来的是回退字体的字宽（比真字体高 5 px 左右）。
- **结果怎么取回**：注入一段探针（`tools/aidogui/aid_probe.js`）在 `document.fonts.ready` 之后把 JSON 塞进 `<script id="embark-probe" type="application/json">`，工具从 `--dump-dom` 里抠出来。第一次抓不到会自动把 `--virtual-time-budget` 从 3000 ms 抬到 8000 ms 再试。
- **`export.ps1` 每次都会重写每个生成文件的头行**（`Generated by AiDoGUI Designer on <时间戳>`），所以重导出后 `git diff` 里会有 23 个文件的 1 行时间戳变化 —— 提交时和真改动一起提交，别单独提交它们。
- **工具不做的事**：不新建屏（只合并进已存在的屏）、不碰图片 / 变量 / 动画 / 多语言（`.aid` 里这些区原样保留）。
  新屏由 `tools/new_app.mjs --apply` 顺带补一张**占位屏**（抄 `album` 的 7 个控件），所以新 App 天生就有起点：
  `--from-aid <App 名>` 拿到草稿 → 改 → `--html … --apply` 量回同一张屏。要一张完全空白的屏才去设计器建。

---

## 7. 相关

- [tools.md](tools.md)：验收开关与退出码、`export.ps1` / 字体脚本、给仓库加新脚本的五条规矩；§2 的 `dev.ps1` 是「`--apply` 之后立刻看到界面」的那条命令
- [../../app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)：设计工程目录、字体与真机资源约定
- [draft-template.html](../../tools/aidogui/draft-template.html)：能跑通的最小草稿骨架（抄片段 / 验证工具链，见 §1）
- [new-app-guide.md](new-app-guide.md)：加一个带界面的 App（含屏侧待办）
