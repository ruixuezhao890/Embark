# 03 删 BIN 生成链与拷卡流程

Status: done（2026-10-17 落地；真机实跑待 idf 环境恢复）
Stage: 后删
Blocked by: 01

## 做什么

- 删 `tools/aidogui/gen_fonts_bin.mjs`（291 行，SIZES 7 档 × 4 bpp、补字并集、回读 cmap 校验）。
- 删 `app/aidogui_ui/export/font/*.bin`（7 个，合计 68,700 B）与 `export.ps1` 的 `$FontFormat='BIN'`
  及末尾的 `node gen_fonts_bin` 补字调用（`tools/aidogui/export.ps1:29-30,62,131-139`）。
- `cmake/embark_aidogui_tables.cmake:89,144-149`：从"正则抓 (符号, bin 路径)"改成"抓字号"，
  entry 变 `{int size; lv_font_t** slot;}`。
- 清理隐式依赖：`tests/CMakeLists.txt:78`（fixture 假设 export/font 存在）、`.github/workflows/ci.yml:80-91`。
- 清 `esp32/project/sdkconfig.defaults:29` 里长文件名 BIN 的注释。

## 验收

- 干净 clone + 构建，仓库内搜不到 `ui_font_ui_heiti_* .bin` 的加载路径。
- 设计器导出一次，不需要任何"拷字体到卡"的手工步骤。

## 落地（2026-10-17）

### 删了什么

- `tools/aidogui/gen_fonts_bin.mjs`（291 行）整个删掉。
- `app/aidogui_ui/export/font/` 7 个 BIN（68,700 B）删掉；`tools/aidogui/export.ps1` 的 `$FontFormat` 参数
  与末尾 `node gen_fonts_bin` 补字块一并删除。
- `tests/host/test_font_service.cpp` 删掉（它只能靠真实 BIN 跑，BIN 一走必红）。顺手把 `embark_host_tests`
  的 `target_link_libraries` 去掉 `embark::common_ui` —— 它会把 LVGL 与 `embark_zh_14` 拖进来，静态库顺序
  （`--whole-archive` 的 `embark_zh_font.a` 排在 `liblvgl.a` 之后）会报 `undefined reference to
  lv_font_get_glyph_dsc_fmt_txt`；而这个可执行文件只测宿主文件系统，本就不需要 UI。

### 怎么让生成代码不再要 BIN

生成物不能手改（`export.ps1:62` 会清空 `export/` 重生），所以两头一起做：

1. `cmake/embark_aidogui_tables.cmake` 不再抓 `lv_font_load("路径")`，改抓 `ui_font_*` 的**符号定义**取字号：
   `MATCHALL "lv_font_t[ \t]*\\*[ \t]*ui_font_[A-Za-z0-9_]+[ \t]*=[ \t]*NULL"` → 符号尾部 `_([0-9]+)$` 即字号；
   entry 变 `{int size; lv_font_t** slot;}`。配置期实测输出 7 行「字体 11 px … 56 px」。
2. `export.ps1` 在生成器跑完之后把 `ui.c` 里的 `aidogui_fonts_init/destroy` **打桩**（两个空函数体 +
   `return true`），删掉 `font/` 目录，并在结果里仍能搜到 `lv_font_load` 时 `throw`（生成器模板一变就炸，
   不静默）。写入必须走 `[System.IO.File]::WriteAllText(..., UTF8Encoding($false))` —— PS 5.1 的
   `Set-Content` 会按 GBK 重写、毁掉中文注释。
3. `app/aidogui_ui/export/ui.c` 已按同一正则打桩落库：163 行 → 94 行，7 个 `lv_font_t * ui_font_ui_heiti_NN = NULL;`
   保留（cmake 靠它们取字号），两个字体函数只剩注释。全仓 `export/` 已无 `lv_font_load`。

### 桥的改动

- 删 `g_ttf_fonts` 与 `bin_path_px()`；`load_ttf_fonts` 直接读 `font.size`，槽指针用 `*font.slot`；
  删掉 init 里整段 BIN 兜底，destroy 只剩 `lv_tiny_ttf_destroy`；`aidogui_ui_bridge_font(size)` 改成按槽名字号查。
- **顺手修掉一个真 bug（无卡会段错误）**：把 TTF 改名后跑 `--ui` 直接 exit 139。生成代码把字体指针
  **显式设进样式**，槽是 NULL 时绘制期解引用空指针（LVGL 只在"属性没设过"时才回落 `LV_FONT_DEFAULT`）。
  BIN 时代没插卡同样是这个空洞。修法：`loaded == 0` 时把 7 个槽全指到 `LV_FONT_DEFAULT`
  （`const_cast<lv_font_t*>(LV_FONT_DEFAULT)`，内置字体是 const），destroy 里 `*font.slot == LV_FONT_DEFAULT`
  就跳过释放。

### 验收证据

- `cmake --build build` exit 0；`ctest --test-dir build` 2/2 通过。
- 五条故事全绿（TTF 在位）：〔aidogui 验收通过〕〔aidogui 导航验收通过〕（导航命中 3，月历基准 2026-10）
  〔aidogui 天气验收通过〕〔主屏分页验收通过〕（磁贴 6 / 页 2 / 点 2，亮度档 0→1）〔eez 验收跳过〕。
- 无卡（TTF 改名）：不再崩，打出「7 个槽全指向内置字体」的 WARN 并跑到底；两条故事报"验收失败"是因为
  **故事自己的字形/排版断言**依赖真实 TTF（内置 14 px 字体覆盖不了 22/48/56 的文案与图元），属于预期降级态，
  不是崩溃 —— 已在 ADR 0017 第 8 条写清。
- `tools/aidogui/export.ps1` 语法自检通过（`Parser::ParseFile`，1012 token），并把打桩正则单独对 HEAD 的
  原始 `ui.c` 跑了一遍冒烟：163 → 90 行、7 个槽保留、无 `lv_font_load` 残留。
- clang-format 18.1.8：`app/common/aidogui_ui_bridge.cpp`（`-i` 修了 1 处）、`.h` 均 `--dry-run --Werror` 通过；
  `export/` 是生成物不在其列。
- 真机侧只做 TU 级语法校验（idf 环境仍坏：ESP-IDF python venv 缺、`idf_tools.py` 报 xtensa-esp-elf 无已装版本）：
  用 `build-esp32/compile_commands.json` 的真实 argv 对 `app/common/aidogui_ui_bridge.cpp`、
  `app/aidogui_ui/export/ui.c`、`screens/ui_about.c` 跑 `xtensa-esp32s3-elf-g++ -fsyntax-only` 全部 OK
  （脚本留在 `.scratch/font-ttf/tucheck.cjs`，`ROOT=. node .scratch/font-ttf/tucheck.cjs <目标…>`）。
  注意：`build-esp32` 里那份生成的 `embark_aidogui_ui_tables.h` 是旧配置的产物（还是 `bin_path`），
  已用宿主新生成的那份覆盖后再校验 —— 真机重配时会自动重生，不影响仓库。

### 还没做（留给后续 issue）

- 文档里仍在讲 BIN 的地方（`app/aidogui_ui/README.md:141,166`、`platform/esp32/README.md:141,147,240`、
  `platform/esp32/src/esp32_board.h:148-150`、`platform/esp32/src/esp32_font_probe.cpp:307`、
  `platform/common/font_service.h:51`、`platform/common/font_alloc_scope.h:9`）归 issue 06 统一改口径；
  `docs/adr/0007`、`docs/adr/0016` 是历史正文，不改。
- `.github/workflows/ci.yml` 的 font-audit job 本身不读 `export/font`，只需换注释口径（issue 06）。
- 真机实跑（插卡 → 7 档 TTF 起得来 / 缓存账 / 断卡降级）待 idf 环境恢复。
