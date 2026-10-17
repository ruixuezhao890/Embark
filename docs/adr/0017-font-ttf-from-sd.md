# 字体：SD 卡上的整份 TTF + 运行期栅格化（单 face / 7 个字号槽 / 按字号配额）

**状态**：已定（`2026-10-17`）。**取代** ADR 0016 的第 2、5 条（TTF 进 flash 分区 + mmap、字体随固件分发）与
ADR 0007 的第 3、5 条（按文件字节记账、子集覆盖审计的原形态）。ADR 0016 的实测数据继续有效 ——
本 ADR 改的是"字从哪来、几档、多少缓存"，不改"用 stb 栅格化"这个结论。

## 背景与触发

ADR 0016 已经证明运行期栅格化可行且比 BIN 快（真机一屏 120 汉字 14 px：首帧/热帧 56.8/35.6 ms，
卡上 BIN 是 128.5/123.0 ms）。它的落地方式是把 3.85 MB TTF 烧进 4 MB 的 `fonts` flash 分区、启动时 mmap。
这一场 grilling（11 问）否掉了这个落地方式，理由与结论如下。

两个新事实决定了走向：

1. **字体今天就已经在 SD 卡上**。`'S'` 盘的真机根是 `/sdcard`
   （`platform/esp32/project/components/embark/CMakeLists.txt:87`），7 个 BIN（合计 68,700 B）从第一天起
   就是从卡里读的；flash 里真正只有 76 KB 的 `assets/fonts/embark_zh_14.c`。改 TTF **不是引入新的分发路径**，
   是把新机制接到既有路径上。
2. **SD 上没有 mmap**。FATFS 没法映射成可随机访问的 RAM 窗口，而 `lv_tiny_ttf` 要求 data 常驻
   （`third_party/lvgl/src/extra/libs/tiny_ttf/lv_tiny_ttf.c:206` 只存指针不复制，本仓 `LV_TINY_TTF_FILE_SUPPORT`
   取默认 0）⇒ 整份 TTF 必须进 RAM。

## 决策

1. **字体只从 SD 卡读，不进 flash 分区**（用户理由：flash 更珍贵，另有他用）。⇒
   `platform/esp32/project/partitions.csv` 里那行 `fonts, data, 0x40, 0x310000, 4M` 删除，
   `platform/esp32/src/esp32_font_probe.cpp` 的 mmap 段改走 SD。（已落地，issue 05：分区表只剩
   nvs / phy_init / factory，探针改成量产品路径、不再自己建字体。）
2. **现在就做，先通后删**：先让 TTF 通道顶替全部字号、6 张屏零视觉回归；再删 BIN 生成链与拷卡流程。
   理由：阅读器与 BIN 链并行改最贵，且"忘了拷卡 → 静默方框"这条坑在整本书的字符量下放大 100 倍。
3. **语言边界**：锁中日韩 + 拉丁；西里尔/希腊/泰文推迟；RTL（阿拉伯/希伯来）标为可选、需实测后决定；
   复杂印度系 shaping 与竖排直排 **明确不做**。
4. **单 face，不接 fallback 链**。`lv_font_t::fallback` 在 8.3.11 是能用的
   （`third_party/lvgl/src/font/lv_font.c:79-93` 沿链走），但 `lv_tiny_ttf.c:225` 建出来的字体 `fallback = NULL`，
   接链要手写，且每个 face 一份常驻缓冲。没有单一字体同时覆盖 CJK 与泰文 ⇒ 要泰文就必须多 face，推迟。
5. **7 个字号槽一个都不动**。`app/aidogui_ui/export/screens/*.c` 有 75 处直接引用
   `ui_font_ui_heiti_{11,12,14,22,28,48,56}`。**槽是间接层** —— 只换槽里塞什么，7 张屏零改动。
   注意 `export/ui.c` 是设计器生成物（`tools/aidogui/export.ps1:62` 会清空 `export/` 重生），
   改加载逻辑必须落在桥或后处理，手改会被覆盖。
6. **每字号一份字形缓存配额**（Glyph cache budget），按"该字号一屏最大并发字形数 × w×h"反推，
   实测值见下表，合计 118 KB < 字库区 192 KB。配额是**上限不是目标**；超限按"先砍 56、再砍 48、再砍 28…"
   的顺序降级，绝不出现"预算 > 可用池"（ADR 0016 那次 64 KB 翻车就是这一条）。
   实现：`app/common/aidogui_ui_bridge.cpp` 的 `kCacheQuotaDefaults` + `fit_cache_quotas()`，init 时读
   `font_alloc_pool_budget_bytes()`（池的计划容量，见 `platform/common/font_alloc_scope.h`）按池的 3/4
   收一次配额 —— 池里字形实占"16 字节对齐位图 + 48 字节块头"≈ 记账值的 1.3 倍。
   - 与 grilling 里"砍掉 48/56 两档"的建议不一致，这里说明为什么改：收尾重算时发现屏幕上 48/56 px 的字形
     总数是个位数到十几个（不是 200 个），小配额装得下；而砍档意味着 75 处引用里那几处要改指向，是**视觉变更**。
     零视觉变化优先 ⇒ 保留 7 档 + 小配额；实测超预算再砍。
7. **整份 TTF 常驻 PSRAM**（3,854,132 B），不打开 `LV_TINY_TTF_FILE_SUPPORT` 按需读卡：那会把"启动慢一次"
   换成"每屏首次翻页都慢"，且 file 分支在本仓从未跑过。代价必须与 PSRAM 分账一起验（见"未做"）。
8. **无卡 / 字体缺失的降级**：退回内置 `embark_zh_14`（14 px、184 字形）+ `ELOG_WARN`，其余字号退化到
   14 px；阅读器必须有显式的"字体未就绪 / 未插卡"态。
   实现上不能只是"加载失败就不填槽"：生成代码是 `lv_obj_set_style_text_font(obj, ui_font_ui_heiti_14, …)`
   **直接塞指针**，槽为 NULL 时绘制期解引用空指针（issue 03 实测宿主段错误），LVGL 只在"属性没设过"时才
   回落到 `LV_FONT_DEFAULT`。所以兜底要把 7 个槽全指到 `LV_FONT_DEFAULT`（`= &embark_zh_14`），
   destroy 时按"等于内置字体就不归桥释放"跳过。BIN 时代没插卡同样会崩，只是没人踩到。
9. **`font_service` / `font_ledger` / `embark_limits.h` 三个常量整体退役**。它们唯一的消费者是
   `platform/esp32/src/esp32_bringup.cpp:130` 的探针，桥从来没走过；"路径 + 文件字节"的账在 TTF 时代没有
   对应物（记账对象变成字形缓存字节 + 字号）。容量控制挪到新的字体管理器，两套测试跟着重写。
   已落地（issue 04）：四个文件与三个常量删除，启动期自检从"自己 `lv_font_load` 一份探针字库"改成
   直接读桥的字体报告（`aidogui_ui_bridge_font_report()` 的 `loaded == total`，令牌仍是 `SD_FONT_OK`）；
   测试换成 `tests/host/test_ttf_font.cpp`（真 TTF 建实例 + 极小缓存仍出位图）。理由：桥已经在
   `framework.boot()` 里把 3.85 MB 读进 PSRAM，探针再读一份会在 8 MB 上顶到 7.7 MB。
10. **断行沿用 LVGL 现状，缺口记账**。8.3.11 的 CJK 逐字断行只认 U+4E00–U+9FA5
    （`third_party/lvgl/src/misc/lv_txt.h:165-184`），且全仓无行首禁则 ⇒ 标点可以起行，假名/谚文/CJK 扩展区
    不算断点。先让阅读器跑起来，把"标点起行"当已知缺陷；真需要时由 reader 自算断行，不升 LVGL 9。
    **2026-10-17 已实测钉住**（issue 07）：`tests/host/test_text_wrap.cpp` 覆盖三种文本 —— 中文逐字断且
    不丢字、全角标点起行、假名/拉丁长串按宽度硬切不溢出；LVGL 哪天改了断行，这三条会先红。

## 内存账（本 ADR 最容易被忘的一段）

`lv_tiny_ttf` 位深固定 8 bpp（`third_party/lvgl/src/extra/libs/tiny_ttf/lv_tiny_ttf.c:125`），
单字形位图 ≈ w×h 字节：

| 字号 | 位图/字（估） | 实测一屏并发字形 | 配额 | 约可容字形数 |
| --- | --- | --- | --- | --- |
| 11 px | ≈121 B | 9 | 6 KB | ≈50 |
| 12 px | ≈144 B | 32 | 12 KB | ≈85 |
| 14 px | ≈196 B | 32 | 32 KB | ≈165 |
| 22 px | ≈484 B | 5 | 16 KB | ≈33 |
| 28 px | ≈784 B | 5 | 16 KB | ≈20 |
| 48 px | ≈2.3 KB | 5 | 16 KB | ≈7 |
| 56 px | ≈3.1 KB | 5 | 20 KB | ≈6 |

合计 118 KB（issue 02 落地时定稿，替代起草的 116 KB）。"实测一屏并发字形"= 五条验收故事
（--ui / --ui-nav / --ui-weather / --ui-launcher-pages / --eez）里同一字号画过的不同码点数最大值
（宿主临时统计，量完已删）。两处据此改正：12 px 从 8 KB 抬到 12 KB（32 个字 × ≈216 B 实占 ≈7 KB，
原值卡在池沿上）；48/56 px 从各 8 KB 抬到 16/20 KB —— 原值只装得下 3 个字形，而一屏要 5 个，
每帧白扔 5 次重栅格化（真机 405 µs/字 ≈ 2 ms/帧）。阅读器那种一屏 300 字的场景留给 issue 07 重算 14 px。

PSRAM 分账（8 MB）：整份 TTF 3.85 MB + 字形缓存池 192 KB（实占 ≈153 KB）≈ 4.06 MB，剩约 3.9 MB
要给 LVGL 绘制缓冲、AI 讲书音频以及其余模块。**池的实际占用仍是估值口径**（池里每块 = 位图 + 48 字节
块头），真机观测口已补：init 日志打「字体缓存：已用 X / 字库区 Y 字节」。

## 代价与影响（诚实账单）

- **首次出现代价 878 µs/字（14 px，真机实测）**。比 ADR 0016 分区 mmap 时代的 405.6 µs 慢一倍，热命中则几乎不变
  （19.48 vs 19.7 µs）⇒ 差异只在第一次栅格化；推断是字体源从 flash mmap（XIP 走缓存）换成了 PSRAM 常驻，
  没做 A/B，不算结论。一页 300 个新字 ⇒ 首屏约 260 ms。
- **无卡 = 界面只有 14 px**：与 BIN 时代相同，但阅读器从此完全依赖卡。
- **启动多 9 秒**：真机实测从 SD 读 3,854,132 B 用 8,832 ms（0.44 MB/s @ 10 MHz SPI 单线），启动时间线
  1244 ms 打完配额 → 10177 ms 字体源就绪，首个 App 10365 ms 起画面（`.scratch/font-ttf/hw/boot-timed.log`）。
  **2026-10-17 决定：接受这个代价，配一块启动占位屏**（`aidogui_ui_bridge.cpp` 的 splash_* 系列，全英文、
  由 static_assert 钉住 ASCII）。按用户要求做成开机自检（POST）的样子：`[OK]/[..]/[!!]/[  ]` 逐行打勾、
  整行配色、右下角 `t=<elapsed> ms`；内容全真（分辨率 / 字库区容量 / 卡挂载点 / 字体进度 / UI 就绪），
  真出问题打红而不是假装绿。硬件自检行由平台注入（`aidogui_ui_bridge_set_boot_checks()`，最多 6 行，
  `main.cpp` 填 flash / psram / display / touch / sdcard / font pool），**每秒翻绿一行**，翻的动作发生在
  读卡那 9 秒的进度回调里 ⇒ 零额外启动时间。踩到的坑：`lv_tick` 在阻塞期是冻住的（`LvglPort::tick()`
  只在 UI 循环里推进），节奏必须用平台时钟 `esp_timer_get_time()` 由 `set_boot_checks()` 的时钟回调注入。
  证据见 `.scratch/font-ttf/issues/08-hardware-acceptance.md` 的「启动占位屏」一节。
  没采纳的替代：提 SPI 频率、TTF 回 flash 分区、首次启动「装机」到 flash —— 见「否决过的方案」。
- **断行缺口**：见第 10 条，阅读器排版仍需一次自己的活（行为已由 `tests/host/test_text_wrap.cpp` 钉住）。
- **授权**：字体不再随固件分发，暴露面比"整份 TTF 烧 flash"小得多；但产品出货若预置字体仍然要授权。
  `ui_heiti.ttf`（时尚中黑简体 TRENDS）授权待确认，开源替代是 Noto Sans SC / 思源黑体。
- **文档债**：`docs/reference/limits.md:100-102`、`GLOSSARY.md` 相关条目、`platform/esp32/README.md:141-147`
  与 `app/aidogui_ui/README.md:141,166` 全在描述 BIN 链。

## 否决过的方案

- **提 SPI 频率省启动时间**（10 → 20/40 MHz）：实测 0.44 MB/s 是 10 MHz 单线的正常水位，提频最多把 9 s 压到
  2~4 s，仍不是「秒开」；换卡兼容性风险另算。用户选了「接受 + 占位屏」，这条留着当后续选项。
- **TTF 烧 4 MB flash 分区 + mmap**（ADR 0016 原案）：用户的理由是 flash 有别的用处。技术上它反而是本 ADR
  第 7 条的更优解（零 RAM 常驻），放弃它是资源取舍不是技术判断 —— **将来 PSRAM 吃紧，这条可以翻回来**。
- **多 face + fallback 链**：为泰文/西里尔付"多份常驻缓冲 + 手接 fallback"的账，而目前没有具体需求。
- **打开 `LV_TINY_TTF_FILE_SUPPORT` 按需读卡**：把冷栅格化变成"栅格化 + 若干次 SD 随机读"，且该分支本仓从未跑过。
- **升级 LVGL 9 换排版层**：迁移面远大于字体这一件事，且没有证据表明 9 解决了行首禁则。
- **继续 BIN、只把字集铺满**：全字集 7,582 字 × 7 档 4 bpp ≈ 26 MB，且不防"卡上旧文件"。

## 未做（前置与后续）

- PSRAM 分账实测（池的实际占用只有估值口径，缺真机读数）。从 SD 读 3,854,132 B 的耗时已测（8,832 ms，
  见「代价与影响」）。
- 真机「300 字/页 × 20 页」连续翻页验收（宿主无法代表 PSRAM 带宽与淘汰抖动）。
- 11/12 px 是否还需要（TTF 下可随时生成，但要复测墨量）。
- 换字体文件的流程（拔卡重插、卡里换了一份 TTF 怎么办）。
- 行首禁则本身（阅读器侧）：LVGL 的行为已实测钉住（issue 07），真要治得 reader 自算断行；
  今天仓库里还没有阅读器 App，所以只记账不实现。
- 授权确认或换体。
- ~~文档与审计脚本的语义切换~~ —— 已随 issue 03/04/05/06 完成。

工作项见 `.scratch/font-ttf/`。
