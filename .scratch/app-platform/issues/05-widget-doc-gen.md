# 05 · 接口文档生成器

Status: open
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
