# 05 · 接口文档生成器

Status: done（2026-10-18）
Blocked by: 03

## 为什么

用户 m00075："把要交互数据的**接口和地方**给我一个文档"。

这不是要新建的东西，是要**打印出来的东西**——三份数据已经存在，只是没人 join：

| 数据 | 在哪 |
| --- | --- |
| 屏与控件（设计真相） | `app/aidogui_ui/screen.aid` 的 `screens[].widgets[]` |
| 控件表（C 符号真相） | 构建目录 `include/embark_aidogui_ui_tables.h` 的 `kAidoguiWidgets_<屏id>[]` / `kAidoguiScreens[]` |
| 实际使用（代码真相） | `app/*/*.cpp` 里 `aidogui_ui_bridge_widget("<屏>", "<控件>")` 的调用 |

三者 join 的结果就是用户要的文档，而且白捡一个副产品：**哪些控件没人写 = 死控件**。

## 交付物

`tools/aidogui/gen_widget_doc.mjs`（Node、零依赖）→ 生成 `docs/reference/ui-widgets.md`

每个屏一节，每个控件一行：

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `L_WTemp` | lv_label | 24,120 60×28 | `--` | — | `app/weather/weather_app.cpp:88` |
| `Btn_CityNext` | lv_obj | 190,20 40×32 | `>` | — | `app/weather/weather_app.cpp:141` |

末尾两节：**死控件清单**（.aid 里有、代码里没人取）与**孤儿引用**（代码里取了、.aid 里没有——这种今天会静默返回 nullptr）。

## 接在哪

- 作为 `tools/` 下的独立脚本，**不进构建**（避免每次构建都改 docs 产生 git 噪音）
- 但要在 `app/aidogui_ui/README.md` 里写一行"改完屏跑一次"

## 验收

- 跑一次，`docs/reference/ui-widgets.md` 覆盖全部 6 屏 124 控件
- 死控件清单与人工抽查一致（`music` / `album` 两屏的 `Card_Placeholder` 系列应当出现在里面）
- 孤儿引用为空（今天应当是 0；不为 0 就说明有 bug）

---

## 收尾记录（2026-10-18）

### 落地的文件

| 文件 | 说明 |
| --- | --- |
| `tools/aidogui/gen_widget_doc.mjs` | 新增，零依赖 Node ESM。生成 / `--check` / `--aid` / `--export` / `--out` / `--src` / `--help` |
| `docs/reference/ui-widgets.md` | 生成物，290 行，别手改 |
| `app/aidogui_ui/README.md` | 新增「改完界面跑一次控件表」一节（正文写清「控件改名不会编译失败，只会静默 nullptr」） |
| `docs/guides/tools.md` | §0 一览表加一行，「六个脚本」→「七个脚本」 |
| `docs/index.md` | reference 表加 `ui-widgets.md` 行 |

### 三处偏离票面（都是实测逼出来的）

1. **不用构建期表头，改扫 `export/screens/*.h`**。票面写「控件表真相 = 构建目录 `include/embark_aidogui_ui_tables.h`」，但那要构建过才存在，而且有三份副本（`build/`、`build-esp32/`、`platform/esp32/project/build/`）。`app/aidogui_ui/export/screens/ui_*.h` 是入库的设计器产物，同样含 `extern lv_obj_t * ui_<屏>_<控件>`，零构建依赖。构建期表头只用来判**导出目录过期**（.aid 有、export 没有对应符号）。
2. **归因不能只抓字面量**。票面假设调用点长成 `aidogui_ui_bridge_widget("weather", "L_WTemp")`，实际 app/ 里 23 处调用绝大多数是常量、表驱动或 `snprintf` 拼名（`kClockScreen` / `entry.widget` / `"L_D%u_Dow"`）。只抓字面量会把 clock 的 13 个、weather 的 20 个控件误报成死控件。实现改成三步归因：直连 → 兜底（同文件/同屏的字面量集合 + 格式串正则）→ 排序；并单独列出**认不出来的调用点**（8 条）承认局限。
3. **「死控件」不能只看代码**。设计器里挂了 `CLICKED → embark_ui_open_*` 的控件（各屏 `Btn_Back`、launcher `Card_Weather`）在 C 里没人碰，但它们确实在工作。有设计器事件的控件直接算「在用」，不进死控件清单。

另外加了三节票面没要求的：**只被验收脚本用**（`platform/host/` 读了、App 没读 —— 这可能是漏写）、**导出目录过期**、**认不出来的调用点**。

### 验收实测

| 票面验收 | 实测 |
| --- | --- |
| 覆盖 6 屏 124 控件 | ✅ 概览表 14 / 27 / 37 / 7 / 7 / 32 = 124，逐屏逐控件全列 |
| 死控件清单与抽查一致 | ✅ 49 个，`music` / `album` 的 `Card_Placeholder` + `L_Placeholder` + `L_PlaceholderHint` 六个都在（各屏 `Desk`、`L_Title`、`*_L` 之类装饰控件也在） |
| 孤儿引用为空 | ✅ 0 条 |

附加实测：`node tools/aidogui/gen_widget_doc.mjs --check` → `最新：docs/reference/ui-widgets.md 与源码一致`（exit 0）；重跑两次字节一致（输出无时间戳）；`cmake --build build` 无工作可做、`ctest` 2/2、`--ui --frames 40 --delay 40` exit 0〔aidogui 验收通过〕。

### 一个副产品（值得记住）

票面以为孤儿引用「今天应当是 0」，实测确实是 0 —— 也就是说 .aid 与 C 侧目前没有漂移。49 个死控件里 25 个是容器 `lv_obj`（给子控件当爹，本来就不需要代码），24 个是 `lv_label`。这 24 个里绝大多数是**故意的**（`L_Title` / `Btn_Back_L` / `L_SecDisplay` 这类写死在设计器初值里的装饰文案），剩下的是 `music` / `album` 两张占位屏的 `L_Placeholder` / `L_PlaceholderHint`——占位屏没有任何数据要刷，属预期。**真正该报警的是「该随业务变的 label 没人写」**，现在一个都没有。
