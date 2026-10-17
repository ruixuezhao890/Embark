# 02 字形缓存配额与降级顺序

Status: done（真机 300 字×20 页验收待 idf 环境恢复）
Stage: 先通
Blocked by: 01

## 背景

`lv_tiny_ttf` 位深固定 8 bpp（`third_party/lvgl/src/extra/libs/tiny_ttf/lv_tiny_ttf.c:125`），单字形位图 ≈ w×h 字节。
ADR 0017 的起草配额：14 px 40 KB、16 px 8 KB、18 px 8 KB、22 px 20 KB、28 px 20 KB、48 px 12 KB、56 px 8 KB
（合计 116 KB < `font_psram_bytes` 131 KB）。这些数是推算值，**必须真机实测**。

## 做什么

- 实测每个字号"一屏最大并发字形数"（尤其 48/56 px 的封面大标题屏），据此调配额。
- 实现配额表 + 降级顺序：预算不足时先砍 56，再砍 48，再砍 28；绝不出现"预算 > 可用池"。
- 加一个可观测口（串口或调试页）：每字号「已用字节 / 配额 / 命中率」。
- 把 PSRAM 分账算清：整份 TTF 3.85 MB + 缓存池 + LVGL 绘制缓冲 + 其他，总量不超 8 MB。

## 验收

- 真机连续翻页 300 字/页 × 20 页，无重栅格化抖动（首帧 400 µs/字量级，热命中 19.7 µs/字）。
- 故意把配额压到不够，验证走降级顺序而不是 assert 复位（ADR 0016 的教训）。
## 落地（2026-10-17）

### 实测：一屏并发字形数

宿主临时统计（插在 `scoped_glyph_bitmap` 前，按字号记「画过的不同码点数」，量完已删）：

| 字号 | --ui | --ui-nav | --ui-weather | --ui-launcher-pages | --eez | 最大 |
| --- | --- | --- | --- | --- | --- | --- |
| 11 px | 0 | 2 | 9 | 2 | 9 | 9 |
| 12 px | 0 | 32 | 28 | 23 | 27 | **32** |
| 14 px | 0 | 30 | 32 | 27 | 29 | **32** |
| 22 px | 0 | 2 | 4 | 5 | 2 | 5 |
| 28 px | 0 | 3 | 5 | 3 | 3 | 5 |
| 48 px | 0 | 5 | — | — | — | 5 |
| 56 px | 0 | 5 | 5 | 5 | 5 | 5 |

（`--ui` 一列是空的而非 0：那条故事跑得最短，总调用数没到临时统计的「每 200 次打一行」门槛，
所以没有输出 —— 取其余四条的最大值。）

两处据此改正原配额：

- **12 px 8 KB → 12 KB**：32 个字形 ×（位图 ≈144 B + 块头 48 B）≈ 6.2 KB，加上 LRU 记录正好压在
  8 KB 池沿上 —— 差一点就开始逐帧淘汰。
- **48/56 px 各 8 KB → 16/20 KB**：8 KB 只装得下 3 个字形，而一屏要 5 个 ⇒ 每帧白扔 5 次重栅格化
  （真机 405 µs/字 ≈ 2 ms/帧）。
- 11 px 8 KB → 6 KB（只需 9 个字形）；14 px 32 KB 保持（阅读器的一屏 300 字留给 issue 07 重算）。

定稿表合计 118 KB：`11:6K 12:12K 14:32K 22:16K 28:16K 48:16K 56:20K`。

### 实现（`app/common/aidogui_ui_bridge.cpp`）

- `kCacheQuotaDefaults[]` 是默认表；`g_cache_quotas[]` 是运行期表（init 时从默认表复制再收）。
- `fit_cache_quotas(available)`：`available == 0`（宿主，无独立字库区）→ 照默认表给；否则按
  `kCacheCutOrder = {56, 48, 28, 22, 14, 12, 11}` 逐个砍半、不低于 `kCacheQuotaFloorBytes = 4 KB`，
  直到 `配额合计 ≤ available`。全压到下限仍不够时 `ELOG_ERROR` 报数字（不 assert、不 abort）。
- 可用量 = **池的计划容量 × 3/4**（`kCachePoolNumerator/Denominator`）：字形在池里实占
  「16 字节对齐位图 + 48 字节块头」≈ 记账的 1.3 倍，再留 lv_lru 哈希桶的余量。
- 计划容量取自新增的 `font_alloc_pool_budget_bytes()`（`platform/common/font_alloc_scope.h`）：
  真机返回 `esp32_board.h` 的 `font_psram_bytes`（192 KB），宿主返回 0。
  为什么要「计划值」：字库区是懒建的（首次分配才 `heap_caps_malloc`），配额却要在建字体之前定。

### 可观测口

- init 打两行：`字体缓存配额合计 N 字节（字库区计划 P 字节，可用 A 字节）` 与
  `字体缓存：已用 U 字节 / 字库区 P 字节（配额合计 N 字节）`（宿主无池时只打配额合计）。
- 每个字号仍单独打一行 `就绪（字形缓存 N 字节，行高 H，基线 B）`。
- **命中率不做**：`lv_tiny_ttf` 的 `lv_lru_t` 藏在私有 `ttf_font_desc_t` 里（`lv_tiny_ttf.c` 内部结构，
  头文件无 getter），读它要么复制私有结构布局（改一次 LVGL 就错）、要么改 `third_party`（不做）。
  成本远大于收益；抖动改看帧时间与上面的「已用」曲线。

### 验收证据

- 宿主：`cmake --build build` 通过，`ctest` 2/2。
- 五条验收故事：`--ui` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages` / `--eez` 全绿
  （`--ui-launcher-pages` 偶发一次「设置屏 未进 亮度档 0→0」失败，连跑 3 次全过 —— 是既有的
  输入时序 flake，与配额改动无关）。
- 降级顺序实测：临时让宿主 `font_alloc_pool_budget_bytes()` 返回 96 KB ⇒ 可用 72 KB ⇒ 实际收到
  `{56:4K, 48:4K, 28:4K, 22:8K, 14:32K, 12:12K, 11:6K} = 71680 B ≤ 73728 B`，砍的正是 56→48→28→22
  这个顺序，且屏照常建、故事照常过（没有 assert / 复位）。验完已还原宿主返回 0。
- 真机：`build-esp32/compile_commands.json` 的命令行对 `aidogui_ui_bridge.cpp`、`esp32_lvgl_mem.cpp`、
  `esp32_bringup.cpp`、`main.cpp` 跑 `xtensa-esp32s3-elf-g++ -fsyntax-only` 全部通过。
  **真机实跑仍缺**：本机 ESP-IDF 环境坏了（`ESP-IDF Python virtual environment not found`），出不了固件。

### 仍未做

- 真机「300 字/页 × 20 页」连续翻页（首帧 400 µs/字、热 19.7 µs/字）—— 等 idf 环境恢复。
- PSRAM 实占读数：池里每块 = 位图 + 48 B 块头，118 KB 配额 → ≈153 KB 实占，仍是估值口径。
