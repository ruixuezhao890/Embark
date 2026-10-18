# 03 · 字形配额公式化（删掉 7 档实测表）

Status: done（2026-10-18）
Blocked by: —

## 为什么

Q16/Q17（m00357 / m00594）。用户否决"7 档焊死"：**实机都用 TTF 了，什么字号都能适配**。设计器本来就对每个不同 `textFontSize` 生成一个槽（`ui.c.njk:28-65`），加字号不需要改 cmake（`cmake/embark_aidogui_tables.cmake:81-113` 自动生成 `kAidoguiFonts[]`）。所以 `kCacheQuotaDefaults` 的静态表是**测量结果，不是物理约束**。

## 改成什么

`quota(px) = max(4 KB, 8 × px²)`

- `8 × px²`：8 个字形 ≈ 一屏最大字号的并发字形数（实测 5–9）
- `4 KB` 下限：小字号（11–22px）按 8×px² 只有 1–4 KB，装不下十几个汉字
- **硬下限不可省**：`lv_lru_create` 拿 `cache_size / (px*px)` 当哈希桶数（`third_party/lvgl/src/misc/lv_lru.c:83`），`lv_lru_hash` 取 `h % 桶数`（`:301`）⇒ 配额 ≤ px² 是**除零 panic**，不是变慢。`8×` 已保证 ≥ 2×px²，只需兜 4 KB。

现有 7 档结果：11/12/14/22/28 → 8 KB；48 → 18 KB；56 → 24.5 KB；合计 82.5 KB（可用 144 KB，1.75× 余量）。再加 72 px → 41.5 KB，总计 124 KB，仍塞得下。

## 改动点

`app/common/aidogui_ui_bridge.cpp`：

- 删 `:155-175` 的四张表（`kCacheQuotaSizes` / `kCacheQuotaBytes` / `kCacheQuotaCount` / `kCacheCutOrder`）与 `:180-187` 的 `quota_slot(px)`
- `cache_quota_bytes(px)`（`:227-230`）→ 纯函数实现公式
- `fit_cache_quotas(available_bytes)`（`:200-225`）→ 遍历 `kAidoguiFonts` 里实际出现的字号列表（而不是 7 项静态表），砍半顺序改成"按字号从大到小"，下限 4 KB 不变，砍到底仍只 `ELOG_ERROR` 不 assert
- `:150-154` 的注释（"实测来源 + 2`6 倍放大"）改写成公式说明

**宿主也要按同一公式**：`platform/host/host_lvgl_mem.cpp:155` 的 `font_alloc_pool_budget_bytes()` 返回 0（走默认表）。公式化后宿主与真机必须同一套算法，否则两边行为分叉。

## 文档同步

`docs/reference/limits.md:111`、`GLOSSARY.md:221/234`（Glyph cache budget）、`app/aidogui_ui/README.md:170-172`、`config/embark_limits.h:100`、`docs/adr/0017-font-ttf-from-sd.md:43-44` 都在描述"7 档实测值"，改成描述公式。

## 验收

- **新增单测** `tests/host/test_font_quota.cpp`：喂 `{0, -5, 1, 11, 12, 14, 22, 28, 48, 56, 72, 200}`，断言 ① 恒 ≥ 4 KB ② 恒 > px²（除零保护）③ 单调不减 ④ 大字号至少装得下 8 个字形
- `tests/` 下 grep `cache_quota|font_report` 从 0 命中变成有覆盖
- `cmake --build build` 与 `ctest --test-dir build --output-on-failure` 通过
- `embark_host_ui --ui --delay 40` 不回归；真机启动日志里"配额合计"从 118 KB 变成 82 KB 左右

## 收尾记录（2026-10-18）

**新增** `app/common/font_quota.h`（纯整数算术、不 include LVGL，namespace `embark::demo::font_quota`）：`glyphs_per_size = 8`、`floor_bytes = 4 KB`、`quota_bytes(int px)` = `px<=0 ? floor : max(floor, px*px*8)`、`QuotaEntry{px,bytes}`、`total_bytes()`、`fit(entries,count,available)`（先按公式填满；`available==0` 直接返回 = 宿主语义；否则循环砍「px 最大且 > floor」的项，`bytes/2` 不低于 floor，全到下限就停手不报错）。头注释写清两条硬约束（`quota ≤ px²` ⇒ `lv_lru.c:83` 桶数 0 + `:301` `h % 0` 除零 panic；`quota < 单字形位图` ⇒ `lv_tiny_ttf.c:158-168` 整字不画）。

**删** `app/common/aidogui_ui_bridge.cpp` 的 `struct FontCacheQuota` / `kCacheQuotaDefaults[7]`（118 KB）/ `kDefaultCacheBytes` / `kCacheCutOrder` / `kCacheQuotaFloorBytes` / `quota_slot(px)`，换成运行期表 `g_cache_quotas[kCacheQuotaCount]`（`kCacheQuotaCount = AIDOGUI_FONT_COUNT > 0 ? AIDOGUI_FONT_COUNT : 1`）+ `g_cache_quota_count`。`fit_cache_quotas()` 改从 `kAidoguiFonts[i].size` 填表再调 `font_quota::fit()`。`cache_quota_bytes(px)` 查不到时返回公式值 —— **不再返回 16 KB 魔数**，顺带修掉旧表「表外字号给 16 KB 但不计入 `cache_quota_total_bytes()` ⇒ 账上合规、实际超发」的缺陷。文件 895 → 866 行。

**新增单测** `tests/kernel/test_font_quota.cpp`（8 个 TEST_CASE，全部通过）：恒 ≥ 4 KB 且恒 > px² / 单调不减 / 大字号装得下 8 字形 / 真实 7 档合计 `66176U` / 容量 0 照公式给 / 容量不够从最大字号砍半且不低于下限 / 容量给一半只砍最大字号 / 字号表为空不崩不写内存。**偏离票面**：票面写 `tests/host/test_font_quota.cpp`，实际放 `tests/kernel/` —— 公式是纯函数，`embark_tests` 只链 core + static_pool + doctest，不必为它把 LVGL/tiny_ttf 拖进 whole-archive，先例是 `tests/kernel/test_ui_screen_names.cpp`。编译坑：range-for 花括号列表需要 `#include <initializer_list>`。

**公式实际值**（比票面估计的 82.5 KB 小）：11/12/14/22 → 4,096（下限）；28 → 6,272；48 → 18,432；56 → 25,088；**合计 66,176 B ≈ 64.6 KB**，对 144 KB 可用池余量 2.2×。

**实测**：`cmake --build build` exit 0（59 目标）；`ctest` 2/2 通过；`embark_tests` 122 cases / 1346 assertions 全过；宿主 `--ui --frames 40 --delay 40` 日志出现「字体缓存配额合计 66176 字节」与「字体缓存：宿主无独立字库区（配额合计 66176 字节）」；五条验收故事 + `embark_host_tour --delay 40` 全 EXIT=0。

**文档同步**：`docs/adr/0017-font-ttf-from-sd.md`（标题 + 第 6 条整条重写 + 内存账表格换成公式结果）、`GLOSSARY.md`（Glyph cache budget 与 Font slot 两条）、`docs/reference/limits.md`、`docs/index.md`、`app/aidogui_ui/README.md`、`app/common/aidogui_ui_bridge.h`、`platform/esp32/src/esp32_board.h`、`config/embark_limits.h`、`platform/esp32/README.md`（样例日志 32768→4096、120832→66176）、`platform/host/ui_demo.cpp`、`platform/esp32/project/main/main.cpp`、`platform/esp32/src/esp32_bringup.cpp`、`platform/esp32/src/esp32_font_probe.h`、`cmake/embark_aidogui_tables.cmake`、`docs/guides/tools.md` —— 一律把「7 档 / 7 个槽」改成「按字号 / 每个 textFontSize 一个槽」。
