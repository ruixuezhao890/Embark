# 05 删 fonts 分区、探针改走 SD

Status: done（2026-10-17 落地；真机 `idf.py partition-table` 与探针实跑待 idf 环境恢复）
Stage: 后删
Blocked by: 01

## 做什么

- `platform/esp32/project/partitions.csv`：删 `fonts, data, 0x40, 0x310000, 4M`。注意文件必须 ASCII-only。
  分区表改动 ⇒ 真机要**重新烧分区表**，不是只烧 app。
- `platform/esp32/src/esp32_font_probe.cpp`：把 mmap 那个分区的那段删掉，探针改从 SD 读 TTF；
  门控 `#if defined(EMBARK_FONT_PROBE)` 保留。
- 顺带清 `platform/esp32/src/esp32_board.h:148-158`、`esp32_bringup.cpp:130` 里指向 BIN 的探针字段。

## 验收

- `idf.py partition-table` 输出里没有 fonts 分区。
- 探针在真机上跑通：从 SD 读 TTF、建实例、打出一屏首帧/热帧数字。

## 落地（2026-10-17）

### 分区表

`partitions.csv` 删掉 `fonts` 那行与它上面 4 行注释，并把尾部注释改成「app 到 0x310000 结束，约 13 MB 未用；
4 MB fonts 分区只是字体 spike 的产物，TTF 搬到卡上后就删了（ADR 0017）」。现在只剩
`nvs`（0x9000/0x6000）、`phy_init`（0xf000/0x1000）、`factory`（0x10000/3M）。文件仍 ASCII-only
（逐字节检查 non-ascii = 0）。

### 探针：从「自己造一份」改成「量产品那份」

旧版探针有两条路都不该再用：mmap flash 里的 fonts 分区（分区没了），以及自己 `lv_tiny_ttf_create_data_ex`
建实验字体（要第二份 3.85 MB TTF，8 MB PSRAM 上桥已经常驻一份，装不下）。重写后：

- **读速**：`measure_sd_read()` 用 `aidogui_res_fs_root()` + 候选 `/fonts/`、`/font/` 找到卡上的
  `ui_heiti.ttf`，32 KB 缓冲滚着 `fread` 读完，打「N 字节，顺序读完 X ms（Y MB/s）」。这是 ADR 0017
  「启动多一次读卡」那条欠账的真机读数来源。
- **栅格化**：不再建字体，直接用 `aidogui_ui_bridge_font(px)` 拿产品实例，扫 U+4E00 起 120 个真命中码点
  拼文本后量冷/热（`timing_count` 封顶 60 个字，避免 28 px 上跑太久），再每个字号量一次一屏首帧/热帧。
  字号清单不自己抄：新增 `aidogui_ui_bridge_font_size_count()` / `aidogui_ui_bridge_font_size(i)` 遍历槽表，
  字号的唯一事实来源仍是生成代码。
- 本底仍是内置 montserrat 14 px 画 120 个 `A`，与前两轮数字可比。

代价：探针现在**依赖桥已经初始化完成**（`total == 0` 时打一行跳过）。可以接受 —— 它要量的本来就是产品路径。

### 一起清掉的 BIN 残迹

- `platform/esp32/src/esp32_board.h`：`sd_font_probe_file` / `sd_font_probe_text` / `sd_font_probe_size` /
  `sd_font_probe_height_slack` 四个字段（issue 03/04 已无人引用）删除，只留 `sd_font_check_fatal`。
- 注释口径：`platform/esp32/project/CMakeLists.txt`（option 说明）、`platform/esp32/src/esp32_font_probe.h`、
  `platform/esp32/project/components/embark/CMakeLists.txt` 里「spike / 判据 4」的说法全去掉。

### 验收

- 宿主：`cmake --build build` exit 0；`ctest` 2/2；五条故事全绿（〔aidogui 验收通过〕〔aidogui 导航验收通过〕
  导航命中 3、月历基准 2026-10；〔aidogui 天气验收通过〕；〔主屏分页验收通过〕磁贴 6/页 2/点 2、亮度档 0→1；
  〔eez 验收跳过〕）。
- `clang-format --dry-run --Werror`：`esp32_font_probe.{h,cpp}`、`aidogui_ui_bridge.{h,cpp}` 全 clean。
- 真机 TU 编译：`.scratch/font-ttf/tucheck.cjs`（真实 xtensa 命令行 + `-fsyntax-only`）对
  `esp32_font_probe.cpp`、`aidogui_ui_bridge.cpp` 通过；并核实 `build-esp32/compile_commands.json` 里那条
  命令带 `-DEMBARK_FONT_PROBE=1`，即探针函数体真被编进去了。
- 收尾那轮 `--ui-launcher-pages` 偶发报〔失败〕（`switches=2`，`值重推 = 错`）：连跑 5 次全通过
  （`switches=4`）。与 issue 02 记的那次同类 —— 宿主输入时序 flake（丢了一次点击，后面那条断言就没事件可查），
  与本次改动无关。
- **没做**：`idf.py partition-table` 输出核对、探针真机实跑 —— 本机 idf 环境坏
  （`ERROR: ESP-IDF Python virtual environment not found`），出不了固件。

工作项索引见 `.scratch/font-ttf/spec.md`。