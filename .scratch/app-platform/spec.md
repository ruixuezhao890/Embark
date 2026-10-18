# App 平台化改造（feature: app-platform）

## 目标

用户原话（m00001）："我不应该所有的 app 都依赖 ai 来创建……让 ai 帮我把硬件的基础设施做好，然后我来编写 app 的逻辑与界面就像在电脑上开发应用一样，全部依赖 ai 我的 token 很难支撑我完全开发。"

动机是 **token 成本**，不是技术洁癖。落成三件事：

1. **工具链** —— 用户画 HTML 草稿，AI 只写语义（这是标题、这是卡片），坐标由浏览器算；控件名与交互接口的文档自动生成，用户照文档写 C++。
2. **运行时框架** —— 让"公共硬件资源"在架构里有名字、有位置。今天 `include/` 27 个头里没有任何服务层概念，`hal::Context` 只有 8 个成员。
3. **公共硬件资源** —— IMU / 音频 / 网络三块今天为零（IMU 有 IBus 无驱动、WiFi 只编进 IDF 无应用代码、I2S 连能力面都不存在）。

## 非目标

- **不做业务服务**（天气服务、音乐服务这类带业务语义的）——Q9 明确排除 (D)。服务层只到"设备 + 公共资源"。
- **不做 C++ 完整热重载**（Q3 排除 (C)）：改 `.cpp` 一律重新构建。
- **不保留 EEZ 任何链路**：不是"留着当参考"，是删掉，因为它会让下一个 AI 读错规则。

## 裁决表（/grill-with-docs 逐条拍板）

| # | 问题 | 取值 | 用户原话 |
| --- | --- | --- | --- |
| Q1 | 用户亲手写的那一半 | (C) C++ 逻辑 + AiDoGUI 画屏，但屏由 HTML 草稿生成 | m00075 |
| Q2 | 基础设施范围 | 工具链 + 运行时框架 + 公共硬件资源 | m00075 |
| Q3 | 热重载档位 | (B) 屏级，不做 C++ 热重载 | m00075 |
| Q4 | EEZ 遗留 | 不算债务，直接删，"不要误导 ai" | m00075 |
| Q5 | HTML 原型停在哪 | (A) 草稿纸：AI 出 HTML → 定稿转 .aid → 之后手改 .aid，HTML 丢掉 | m00145 |
| Q6 | HTML→.aid 谁算坐标 | (B) 无头浏览器量 `getBoundingClientRect()`，AI 只写语义 | m00145 |
| Q7 | "公共硬件资源"的定义 | 物理实机上芯片的功能都算，含音频 | m00145 |
| Q8 | 三块顺序 | ② HTML→屏 流水线 → ① 开发台 → ③ 公共硬件 | m00145 |
| Q9 | 基建停在哪一层 | (C) HAL + 设备服务 + 高层公共资源（IImu / IAudio / INetwork / IGpio），排除 (D) 业务服务 | m00198 |
| Q10 | "公共"的判据 | (C) 按"接口形状稳定"，配硬规则：**HAL 里不许出现芯片型号名**（`IImu` 可以，`IQmi8658` 不行） | m00198 |
| Q11 | 音频拆三层谁进基建 | L1 设备 + L2 流进基建，L3 业务（播放列表/下一首/音量记忆）归用户 | m00198 |
| Q12 | 网络这次做还是第三批 | (A) 完整 INetwork，但排在 IMU + 音频之后 | m00198 |
| Q13 | HTML→屏 用什么渲染器 | Edge 无头（本机已有 `msedge.exe`，零新增依赖） | m00260 |
| Q14 | HTML 里的"交互"怎么写 | 只写 `data-action="xxx"`，不写 JS；动作名原样进 .aid 的 events | m00260 |
| Q15 | 服务层 Context 怎么长 | `hal::Context` 加 3 个可空服务槽，与 display/input 同款 | m00260 |
| Q16 | 字号档位 | **已撤回**：用户否决"7 档焊死"，要求任意字号都走 TTF | m00357 |
| Q17 | 删掉实测表后配额怎么算 | `max(4 KB, 8 × px²)`，外面永远套这层下限 | m00594 |

## 六批清单与顺序

| 批 | 项 | 新接口 / 新工具 | 宿主怎么验 | 真机怎么落 |
| --- | --- | --- | --- | --- |
| 0 | HTML→屏 编译链 | `tools/aidogui/html_to_aid.mjs` | 同一份 .aid | 同一份 .aid |
| 0 | 接口文档生成器 | `tools/aidogui/gen_widget_doc.mjs` | 生成 `docs/reference/ui-widgets.md` | 同 |
| 0 | EEZ 退役 + ADR 重写 + 字体配额公式化 | — | 构建 + ctest | 同 |
| 1 | 开发台 | 屏级重载 / 一键构建运行 | `embark_host_ui` | 不适用 |
| 2 | 服务层骨架 | `hal::Context` 加服务槽 | 与真机同构 | 与真机同构 |
| 3 | 姿态 | `IImu::read()` → accel/gyro | 合成：静止重力 / 正弦摇动 | QMI8658 @ I2C_NUM_1（SDA=11 / SCL=10） |
| 4 | 音频 | `IAudio::play/pause/seek/volume` | SDL2 音频设备 + WAV 解码 | I2S 48k，LRCK=38 / DIN=47 / BCK=48 → PCM5101 |
| 5 | 网络 | `INetwork::state/connect/http_get/sntp` | 直通宿主 socket | `esp_wifi` + `esp_http_client` |

顺序理由：0 批把"AI 要读的规则"先改对（ADR 0002 今天写死"网络与 OTA 不在 v1 范围内"，与 Q9=(C) 完全相反；不改则下一轮 AI 读到的是错的）；1 批降低后续所有迭代的验证成本；2 批是唯一真正的架构改动，必须先于 3/4/5；3/4 不依赖外部世界，能独立验证；5 批最后。

## 第 0 批的三条硬事实（决定了怎么实现）

**① `.aid` 的属性面极小，所以 HTML→.aid 是可穷举的**

`app/aidogui_ui/screen.aid` 全量 6 屏 / 124 控件，用到的 style 属性**只有 14 个**：

`DEFAULT__MAIN` 下 `textFont` 83 / `textColor` 83 / `textFontSize` 83 / `textAlign` 73 / `bgColor` 46 / `radius` 46 / `padLeft` 45 / `padRight` 45 / `padTop` 45 / `padBottom` 45 / `bgOpa` 37 / `borderWidth` 37；`PRESSED__MAIN` 下 `bgColor` 12。

widget 顶层键恰好 14 个：`id / parentId / type / name / x / y / width / height / text / style / events / flags / state / scrolling / align`。颜色统一 `#rrggbbaa`。类型只有 `lv_obj` / `lv_label` / `lv_bar` / `lv_calendar` 四种。

⇒ 编译器要覆盖的 CSS 子集就是这个 14 项属性 + 4 种元素，不需要通用 CSS 引擎。

**② 加字号不需要改 cmake**

`cmake/embark_aidogui_tables.cmake:81-113` 用正则从 `${EXPORT_DIR}/ui.c` 抓 `ui_font_<字体名>_<字号>` 符号，自动生成 `kAidoguiFonts[]`。设计器对**每个不同的 textFontSize 生成一个槽**（`ui.c.njk:28-65`）。所以：改 `.aid` 里某个 label 的 `textFontSize` → 重新导出 → 表自动多一行，C++ 零改动。

**③ 配额公式外面必须永远套一层下限**

`lv_lru_create` 拿 `cache_size / (px*px)` 当哈希桶数（`third_party/lvgl/src/misc/lv_lru.c:83`），`lv_lru_hash` 再取 `h % 桶数`（`:301`）⇒ **配额 ≤ px² 就是除零，真机 panic**，不是变慢。

`max(4 KB, 8 × px²)` 同时满足三件事：桶数 = 8（≥1）、放得下 8 个字形（一屏最大字号通常只有 5–9 个不同字）、小字号有 4 KB 兜底。8× 的倍数本身已保证 ≥ 2×px²，所以只需额外兜 4 KB 下限。

## 工单索引（`.scratch/app-platform/issues/`）

| 票 | 标题 | 批 | Blocked by |
| --- | --- | --- | --- |
| 01 | EEZ 全套退役 | 0 | — |
| 02 | ADR 重写（0002 被取代 / 0012 加前瞻 / 新增 0019） | 0 | 01 |
| 03 | 字形配额公式化（删掉 7 档实测表） | 0 | — |
| 04 | HTML→屏 编译链 | 0 | 02, 03 |
| 05 | 接口文档生成器 | 0 | 03 |
| 06 | 开发台：屏级热重载 + 一键构建运行 | 1 | 04 |
| 07 | 服务层骨架（Context 加三个可空槽） | 2 | 02 |
| 08 | 姿态服务 IImu | 3 | 07 |
| 09 | 音频服务 IAudio（L1 + L2，只做 WAV） | 4 | 07 |
| 10 | 网络服务 INetwork | 5 | 07 |
| 11 | 一键生成 App（AiDoGUI 版脚手架，模板读 app/music/） | 2 | — |

## 关键文件

- `app/common/aidogui_ui_bridge.cpp`：`kCacheQuotaDefaults` 四张表（`:155-175`）删除 → 纯函数 + `fit_cache_quotas()` 改吃实际字号列表
- `include/embark/hal/context.h`：8 成员 → 11（加 `imu` / `audio` / `network` 三个可空槽，放 `hal::` 下不另开命名空间）
- `include/embark/hal/`：新增 `imu.h` / `audio.h` / `network.h`
- `platform/host/` + `platform/esp32/src/`：两个装配点
- `app/eez_ui/`：整目录删除
- `docs/adr/0002-hal-capability-granularity.md`：被新 ADR 取代
- `docs/guides/new-app-guide.md`：删步骤 E（`customCharsetRows` 是死数据，`tools/` 下 0 命中）
- `tools/aidogui/`：新增 `html_to_aid.mjs` / `gen_widget_doc.mjs`

## 验收

每批独立验收，批次内不回归既有四条命令（`--ui` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages`，**必须带 `--delay 40`**）。

## 已知坑（写进 issue 的验收里）

- `music` / `album` 两块屏的返回按钮事件名是 `embark_ui_open_launcher`，但 `app/common/aidogui_ui_hooks.h` 的 `EMBARK_AIDOGUI_HOOK_APPS` 只列了 4 个 App（launcher/clock/settings/weather）。这两个 App 只能靠"屏名 == App 名"的观察者路径切进去；将来它们若要在屏内跳到**别的** App，会链接失败。
- `app/aidogui_ui/screen.aid` 写 `"lvglVersion": "8.4.0"`，`third_party/lvgl` 实际 8.3.11。
- 字形配额逻辑**今天零测试覆盖**（`tests/` 下 grep `cache_quota|font_report` = 0 命中）。
- 宿主 `platform/host/host_lvgl_mem.cpp:155` 的 `font_alloc_pool_budget_bytes()` 返回 0 ⇒ 宿主走默认配额表，不按池收。公式化后宿主也要按同一公式算，否则两边行为分叉。
- **`hal::Context` 是聚合体，加成员编译器不会报错**：C++17 聚合初始化允许初始化项少于成员数，缺的按值初始化（指针 → `nullptr`）⇒ 三个装配点（`platform/esp32/src/esp32_hal.h:67` / `platform/host/host_context.h:58` / `tests/fakes/fakes.h:34`）会静默编译通过。加槽只能靠清单 + grep 验收，不能指望编译器点名。
