# 04 退役 font_service / font_ledger / 三个常量

Status: done（2026-10-17 落地；真机实跑待 idf 环境恢复）
Stage: 后删
Blocked by: 01

## 背景

`platform/common/font_service.{h,cpp}` 的 API 是 `load(path)`/`release(path)`，记账键是路径字符串；
TTF 时代记账对象是**字形缓存字节 + 字号**，路径概念不存在。它唯一的消费者是
`platform/esp32/src/esp32_bringup.cpp:130` 的探针 —— 桥从来没走过它（S2 核查结论）。

## 做什么

- 删 `platform/common/font_service.{h,cpp}`、`include/embark/font_ledger.h`、`src/embark/font_ledger.cpp`，
  以及 `config/embark_limits.h:99/101/103` 的 `font_budget_bytes` / `font_max_loaded` / `font_path_bytes`。
- `platform/common/font_alloc_scope.h` **保留**，但调用方换成新的字体管理器（TTF 常驻 + 每字号缓存都落 PSRAM 池）。
- `tests/host/test_font_service.cpp` 重写为 tiny_ttf 用例（建实例 / 栅格出像素 / 缓存记账 / 降级路径），
  fixture 从 `.bin` 换成 `.ttf`；`tests/kernel/test_font_ledger.cpp` 随记账层一起删或重写。
- `docs/reference/limits.md:100-102` 同步。

## 验收

- 仓库内除历史文档外无 `font_service` / `font_ledger` 引用。
- 测试全绿，且不再依赖 `export/font/`。

## 落地（2026-10-17）

### 删了什么

- `platform/common/font_service.{h,cpp}`、`include/embark/font_ledger.h`、`src/embark/font_ledger.cpp`。
- `tests/kernel/test_font_ledger.cpp`（记账层已无消费者）。
- `config/embark_limits.h` 的 `font_budget_bytes` / `font_max_loaded` / `font_path_bytes`，小节标题改成
  「--- 文件系统（issue 17）---」，只留一行指路注释（容量由 `font_psram_bytes` 与 `kCacheQuotaDefaults` 决定）。
- `esp32_bringup.{h,cpp}` 的整个 `check_sd_font`（194 → 117 行）与 `esp32_board.h` 的
  `sd_font_probe_file` / `_text` / `_size` / `_height_slack`（`sd_font_check_fatal` 保留，语义改成「桥没建出字体时要不要致命」）。

### 启动期自检改看桥

`main.cpp` 不再自己去 `font_service.load()` 探一份字体，而是读 `aidogui_ui_bridge_font_report()`：
`total == 0` 直接跳过；`loaded == total` 打 `[AUTODEBUG_PASS] SD_FONT_OK`；否则按
`sd_font_check_fatal` 决定 `embark::fatal` 还是一条 `ELOG_ERROR`。好处是**只读一次 3.85 MB**
（桥已经读进 PSRAM，探针再读一份会顶到 7.7 MB）。

### 偏离原计划：测试

原文写「`tests/host/test_font_service.cpp` 重写为 tiny_ttf 用例」——该文件在 issue 03 就删了
（它只能靠真实 BIN 跑）。改成新增 `tests/host/test_ttf_font.cpp`：

- `"ttf 能建出字号并栅格出位图"`：14 px 实例取 U+542F「启」的 dsc + 位图；
- `"小缓存下反复取字只换不崩"`：28 px + 1024 B 缓存，5 个字取 3 轮，位图全非空。

测试的 fixture 是 `app/aidogui_ui/assets/font/ui_heiti.ttf`（git 跟踪，CI 一定有），
`tests/CMakeLists.txt` 在配置期检查文件存在（不存在就 `FATAL_ERROR`），再通过编译定义
`EMBARK_TEST_TTF_FILE` 传路径。

### 链接坑（值得记住）

`embark_host_tests` 一加上 tiny_ttf 用例就 `undefined reference to embark_lvgl_alloc / _free /
_realloc / embark_lvgl_assert_failed`——这些定义在 `platform/host/host_lvgl_mem.cpp`，是
`embark_platform_host.a` 的成员。正式程序里 aidogui 桥调 `font_alloc_scope_begin()` 顺手把
那个 .o 拖进来，所以从来没疼过；纯字体测试谁也没引用它，静态库扫一遍就悬空了。

调 `-Wl,--start-group` 没用（lvgl 被 CMake 排在组外，且显式加 `embark_lvgl` 会被去重）。
最终用整包：

```cmake
target_link_libraries(embark_host_tests PRIVATE
  -Wl,--whole-archive embark::platform_host -Wl,--no-whole-archive
  embark::common_ui doctest::doctest)
```

### 验收

- `cmake --build build` exit 0；`ctest` 2/2；`embark_host_tests.exe` = 8 cases / 84 assertions SUCCESS。
- 五条故事全绿：`〔aidogui 验收通过〕` `〔aidogui 导航验收通过〕`（导航命中 3，月历基准 2026-10）
  `〔aidogui 天气验收通过〕` `〔主屏分页验收通过〕`（磁贴 6 / 页 2 / 点 2，亮度档 0→1）`〔eez 验收跳过〕`。
  桥日志仍是 7/7、配额合计 120832 字节。
- `clang-format --dry-run --Werror` 对 13 个改动文件全 clean。
- 真机 TU：`.scratch/font-ttf/tucheck.cjs` 对 `esp32_bringup.cpp` / `esp32_lvgl_mem.cpp` /
  `main.cpp` / `aidogui_ui_bridge.cpp` / `esp32_font_probe.cpp` 全部语法通过（idf 环境仍坏，没出固件）。
- 全仓 `font_service` / `font_ledger` 只剩历史 ADR / GLOSSARY 的「已退役」叙述与
  `docs/reference/limits.md` 的说明段。

### 还没做

- 真机实跑（`SD_FONT_OK` 令牌从未在硬件上验证过）。
- `esp32_font_probe.cpp` 里还留着 `lv_font_load` 与 mmap 分区测试 —— 那是 issue 05。
