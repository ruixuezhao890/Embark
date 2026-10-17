# 01 TTF 灌进 7 个字号槽

Status: done（宿主已验证；真机待插卡实测）
Stage: 先通

## 背景

`app/aidogui_ui/export/screens/*.c` 有 75 处直接把 `ui_font_ui_heiti_{11,12,14,22,28,48,56}` 塞进控件的
`text_font`。这 7 个全局指针对 App 来说是稳定接口，**不要拆**：换字体实现只换"槽里塞什么"。

## 做什么

- 新增字体管理器（位置建议 `platform/common/`，与桥同层）：启动时把 SD 卡上的 TTF 整份读进 PSRAM，
  为实际用到的字号建 `lv_tiny_ttf_create_data_ex(data, size, px, cache)` 实例。
- `app/common/aidogui_ui_bridge.cpp`：把 7 个 `lv_font_load("S:/fonts/....bin")` 换成"向管理器要该字号的实例"，
  写进同样的 7 个全局槽；失败保持现状（`ELOG_WARN` → 退回内置 `embark_zh_14`）。
- 宿主与真机同一份代码：差异只在"TTF 从哪读"（宿主读仓库目录，真机读 `/sdcard`）。

## 验收

- 6 张屏视觉零回归（截图对比 + `line_height`/`base_line`/墨量三项判据）。
- 宿主一键跑通；真机串口打出每个字号的实例地址与缓存配额。
- 无卡启动：7 个槽全部仍指向 `embark_zh_14`，界面能用。

## 风险

- `export/ui.c` 是设计器生成物，改加载逻辑不能手改它 —— 要么改桥，要么在 `export.ps1` 后处理。
- TTF 从 SD 读进 PSRAM 只在启动时做一次，**耗时未测**（ADR 0017 未做清单第 1 条）。

## 落地（2026-10-17）

比原设想少一层：**没有**新增 `platform/common/` 字体管理器 —— 管理器就是
`app/common/aidogui_ui_bridge.cpp` 的 `read_ttf()` / `load_ttf_fonts()`，它本来就是这 7 个槽的唯一持有者，
再抽一层是空壳。

改动（5 个文件 + 2 个平台实现）：

- `platform/common/font_alloc_scope.h`：加 `font_buffer_alloc/font_buffer_free`（常驻 TTF 缓冲，
  真机 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`，宿主 `malloc`）。
- `app/common/aidogui_ui_bridge.cpp`：TTF 整份读进常驻缓冲（候选 `/fonts/` → `/font/` → `/../assets/font/`），
  每个字号 `lv_tiny_ttf_create_data_ex(data, size, px, quota)` 塞进原槽；拿不到 TTF 时原 BIN 循环照跑。
- **字形缓存落在绘制期**（tiny_ttf 的 `get_glyph_bitmap` 里才 `lv_mem_alloc`），那时加载作用域早退出了 ——
  所以给每个实例的 `get_glyph_bitmap` 套一层 `scoped_glyph_bitmap`，让缓存也进 PSRAM 字库区。
- `platform/esp32/src/esp32_board.h`：`font_psram_bytes` 128 KB → 192 KB（7 个槽的 LRU 预算合计 92 KB，
  StaticPool 块头另加约三成）。
- 删 `aidogui_ui_bridge_set_font_scope` 钩子及其在 `main.cpp` 的转发/注册：桥现在直接 include 平台头，
  那层函数指针间接没有存在理由了。

### 验收（宿主）

- `cmake --build build` + `ctest` 2/2 绿。
- `embark_host_ui --frames 40`：`字体源 …ui_heiti.ttf（3854132 字节，常驻 PSRAM）`，
  7 个字号就绪（行高 13/14/16/26/33/57/67，基线 2/2/3/4/6/10/12），`字体 7/7 已加载（TTF 运行期栅格化）`。
- 五条验收故事全绿：`--ui`、`--ui-nav`、`--ui-weather`、`--ui-launcher-pages`、`--eez`。
- 视觉零回归：临时把 TTF 改名走 BIN 基线，同帧截图比对 launcher / clock / weather / settings。
  **排版完全一致**：字宽一致（14 px 日期行 172 vs 173 px）、基线一致（同一 y 起始）、
  断行位置一致（连「18° /26° 折行」这个 BIN 时代就有的瑕疵都一模一样）。
  但**墨量偏重**，实测（同阈值，只数文字像素）：22 px「设置」BIN 401 → TTF 421（+5%），
  14 px 日期行 191 → 222（+16%）。原因不是缩放变了（字宽没变），是 8bpp vs 4bpp：
  BIN 的 4bpp 量化把低覆盖率边缘压成 0，TTF 的 8bpp 保留下来了 —— 视觉上更接近源字体。
  spec 里「墨量差 ≤ 1%」这条按字面**没达到**，属于要不要接受的决定项。
  截图见 `.scratch/font-ttf/shots/`（`compare_weather.png` 单屏对照，`compare_screens.png` 三屏对照）。
- 设备侧编译：三个改动 TU + `esp32_font_probe.cpp` + `esp32_bringup.cpp` 用
  `xtensa-esp32s3-elf-g++ 14.2.0` 对 IDF 5.4 头文件 `-fsyntax-only` 全过。

### 剩（真机）

`idf.py` 全量构建本机跑不了（`idf_tools.py` 报已装工具版本注册缺失，`export.ps1` 起不来），
所以没出固件。真机上把 `ui_heiti.ttf` 拷到 `/sdcard/fonts/` 后要看的就两件事：
串口那两行字体日志、以及首帧/翻页的手感（ADR 0017 未做清单第 1 条：TTF 读进 PSRAM 的耗时还没测）。
