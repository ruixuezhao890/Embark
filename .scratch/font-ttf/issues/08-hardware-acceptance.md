# 08 真机验收（TTF 换装的硬件闭环）

Status: in-progress（2026-10-17：**A 表七行全过**、C 表两条过、B 表读数已出（两处偏离：卡上读 8,832 ms ⇒ 启动多 8.9 s；14 px 冷栅格 878 µs vs spike 的 405.6 µs）。用户决定接受 8.9 s 并配启动占位屏 —— v4「开机自检 + 每秒翻绿一行」已于 2026-10-17 22:3x 上板，**用户口头验收通过**（`hw/boot-checks-final.log`）。剩 D 表 7 屏视觉/触摸、C 表拔卡、卡上 7 个旧 `.bin` 去留。板内是产品固件（探针 OFF）。见 `hw/结果.md`、`hw/boot-checks-final.log`）
Stage: 验收
Blocked by: 01–05、07 都要先 done（已 done，但都只在宿主验证过）

## 为什么这条单独立项

issue 01–07 一次性把整条链在宿主上做完了，可真机侧有六件事从没跑过：

1. 整份 3,854,132 B 的 TTF 从 SD 读进 PSRAM 要多久（ADR 0017「未做」第一条）。
2. 7 档运行期栅格化在 PSRAM 缓存上到底多快（ADR 0016 的 405.6 / 19.7 µs 是**分区 mmap 时代**的数）。
3. 118 KB 配额表 + `fit_cache_quotas()` 的 3/4 规则在真机上成立吗（issue 02 只在宿主用假池验过降级）。
4. 删掉 `fonts` 分区之后固件还能起吗（issue 05 只改了 CSV）。
5. `SD_FONT_OK` 令牌在**新路径**（桥报告而不是自己读字库）上还打得出来吗（issue 04 的验收项）。
6. 7 屏（launcher / clock / weather / music / album / settings / about）在真机面板上的字号与视觉。

同时本机 `idf.py` 环境坏了三个月（`ERROR: ESP-IDF Python virtual environment not found`），
所以这一条一直没法安排。**2026-10-17 摸清了根因并成功编出固件**（见下），现在它只剩"插卡跑一遍"。

## 已经拿到的真机证据（BIN 时代，2026-10-17 13:55，COM3）

来自 `platform/esp32/project/autodebug_reports/diagnostic_report.md`：

- 板子：ESP32-S3 (QFN56) revision v0.2，**Embedded PSRAM 8MB**，USB-Serial/JTAG，
  MAC `d8:3b:da:a0:62:c0`。
- 卡座确实通：`SD：已挂载 /sdcard（SD16G 3504 MB，3674210304 字节）`（16 GB 卡）。
- 那一版桥挂的还是 7 个 `.bin`，日志 `AiDoGUI 字体 7/7 已加载（根 /sdcard）` +
  `[AUTODEBUG_PASS] SD_FONT_OK` —— **SD 通路与自检令牌在旧路径上验过**。
- app 分区 0x300000，当时固件 0xe0af0（920,816 B），剩 71%。

⇒ TTF 时代真正的未知就收窄成三件：**读卡耗时、真机栅格化速度、配额与降级**。

## 环境：为什么一直编不出来，以及怎么绕过

1. `IDF_TOOLS_PATH` 必须是 `E:\00_Software\03_Dev_Env\Espressif\tools`（里面有
   `python_env/` 与 `tools/`），**不是**它上一层的 `Espressif`。填错一层的症状就是
   `ERROR: ESP-IDF Python virtual environment not found`。
2. `export.ps1` 在本机不可用：它用 PATH 上的 `python`（本机 = `C:\Python314\python.exe`）
   跑 `tools/activate.py`，那条 python 解析不到 venv 就 die。用 venv 自带的 python 跑
   `activate.py --export` 是好的（会打 `Activating ESP-IDF 5.4 … Identifying shell: pwsh.exe`）。
3. PATH 上有三个抢道者，IDF 5.4 一个都不认：
   `E:\00_Software\03_Dev_Env\Rust\.rustup\toolchains\esp\xtensa-esp-elf\bin` 的
   `esp-14.2.0_20240906`、cmake 4.0.0、ninja 1.13.2。撞上是 CMake 的硬错误：

   ```
   Tool doesn't match supported version from list ['esp-14.2.0_20241119']:
   E:/00_Software/03_Dev_Env/Rust/.rustup/toolchains/esp/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe
   ```

4. **不要在 Git Bash 里跑 `idf.py`**：会打 `MSys/Mingw is no longer supported`，而且
   `--version` 静默无输出（exit 0）。用 pwsh。
5. 正确姿势 = 让 `idf_tools.py export --format key-value` 吐出 PATH 与四个变量，在**同一个
   pwsh 会话**里应用完再跑 `idf.py`。

## 配方（逐行验证过）

```powershell
$env:IDF_PATH      = "E:\00_Software\03_Dev_Env\IDF"
$env:IDF_TOOLS_PATH = "E:\00_Software\03_Dev_Env\Espressif\tools"
$py = "$env:IDF_TOOLS_PATH\python_env\idf5.4_py3.11_env\Scripts\python.exe"

# 让 IDF 把 PATH 与四个变量吐出来再应用（它会跳过"Not using unsupported ..."那几行噪音）
foreach ($line in (& $py "$env:IDF_PATH\tools\idf_tools.py" export --format key-value)) {
    if ($line -like "Not using*") { continue }
    $i = $line.IndexOf("="); if ($i -lt 1) { continue }
    $k = $line.Substring(0, $i); $v = $line.Substring($i + 1)
    if ($k -eq "PATH") { $env:PATH = $v.Replace("%PATH%", $env:PATH) }
    else { Set-Item -Path ("env:" + $k) -Value $v }
}

& $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 build
# 探针另开一次（会重新 configure，字号表与缓存读数才带 EMBARK_FONT_PROBE）：
# & $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 -DEMBARK_FONT_PROBE=ON build
& $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 -p COM3 flash monitor
```

已验证的部分：`idf.py --version` → `ESP-IDF v5.4`；`... build` → **Project build complete**。

## 验收清单

### A. 启动日志（一条都不能少）

| 日志行 | 判据 / 说明 |
| --- | --- |
| `SD：已挂载 /sdcard（…）` | 必须有。引脚已填：CS IO21 / MOSI IO17 / MISO IO16 / CLK IO14，detect 留 -1；SPI2 @ 10 MHz |
| `字体缓存配额合计 120832 字节（字库区计划 196608 字节，可用 147456 字节）` | 三个数字都要对：192 KB 池 × 3/4 = 147,456，配额表合计 118 KB = 120,832 |
| `AiDoGUI 字体源：/sdcard/fonts/ui_heiti.ttf（3854132 字节，常驻 PSRAM）` | 字节数必须 3,854,132；落点是 `fonts/`，不是退回 `font/` 或 `../assets/font/` |
| 7 行 `AiDoGUI 字体 N px 就绪（字形缓存 Q 字节，行高 H，基线 B）` | Q = 6/12/32/16/16/16/20 KB（11/12/14/22/28/48/56）；行高 = 13/14/16/26/33/57/67，基线 = 2/2/3/4/6/10/12（宿主实测值，BIN 时代 14 px 也是 16/3） |
| `AiDoGUI 字体 7/7 已加载（TTF 运行期栅格化；根 /sdcard）` | 7/7，少一个都不算过 |
| `字体缓存：已用 U 字节 / 字库区 196608 字节（配额合计 120832 字节）` | U ≤ 120,832；U 远小于 118 KB 是正常的（一屏只几十个字形） |
| `[AUTODEBUG_PASS] SD_FONT_OK` | 最后一行；esp32-autodebug 判 PASS 的唯一令牌 |

### B. 探针读数（`-DEMBARK_FONT_PROBE=ON` 另编一次）

| 指标 | 判据 | 参照 |
| --- | --- | --- |
| 卡上 TTF 顺序读速 | 记 ms 与 MB/s；**预期 ~3 s**（10 MHz SPI ≈ 1.25 MB/s 上限，3.68 MB 数据）。> 5 s 就是该考虑提频的信号 | 从没测过 |
| 14 px 冷栅格 | ≤ 450 µs/字 | ADR 0016 真机 405.6（分区 mmap 时代） |
| 14 px 热命中 | ≤ 25 µs/字 | 19.7 |
| 一屏 120 字 14 px 首帧 / 热帧 | ≤ 70 / 45 ms | 56.8 / 35.6（同屏 BIN 是 128.5 / 123.0） |
| 内置 montserrat 14 px 画 120 个 `A` | 29.6 / 26.3 ms ±20% | 测法有没有漂的对照基线 |
| 22 / 28 / 48 / 56 px | 记数（28 px 那档探针只量 24 个字） | 无基线 |
| 收尾 PSRAM 余 | ≥ 1 MB | 8 MB 里 TTF 3.85 MB + 字库区 192 KB + LVGL 缓冲 + 音频 |
| 收尾 字库区已用 | ≤ 196,608 | issue 02 的池 |

### C. 降级（三个方向，都不许 panic）

| 场景 | 期望 |
| --- | --- |
| **拔卡启动** | 桥打 WARN `一个字号也没建出来（源 ui_heiti.ttf），7 个槽全指向内置字体`；界面能进（全 14 px）；没有 `SD_FONT_OK`（esp32-autodebug 判 TIMEOUT=4 而不是 PANIC）；**不重启循环** |
| 卡在、字体不在 / 改名 | 三个候选根（`fonts/` → `font/` → `../assets/font/`）都试过，日志能看到多次 `读不全` / `进不了常驻缓冲`，然后同上降级 |
| 临时把 `esp32_board.h` 的 `font_psram_bytes` 压到 96 KB | 看到砍半后的配额（按 56→48→28→22 依次砍到 4 KB 下限），7/7 仍建出来、界面正常；若砍到底仍超容量，只 ELOG_ERROR 打三个数字、**不 assert、不复位**（issue 02 只在宿主用假池验过） |

### D. 视觉（7 屏）

- `launcher` / `clock` / `weather` / `music` / `album` / `settings` / `about` 逐屏看：
  11/12 px 小字不糊、14 px 正文、22/28/48/56 px 大标题不发虚，**无方框、无裁切、无越界**。
- 与宿主截图对照：`.scratch/font-ttf/shots/` 有 9 张 240×320 PNG
  （launcher / launch26 / ui22 / ui46 / ui74 / ui104 / weather34 / set120 / set128），
  面板同为 240×320，可逐屏对。
- 墨量：TTF 比 BIN 重 5%~16%（14 px +16%、22 px +5%，2026-10-17 实测并已接受）；
  真机不判数值，只判"糊不糊、会不会糊成一团"。
- 触摸顺手验：主屏左右分页、设置屏亮度档、天气城市切换各点一次
  （宿主 `--ui-launcher-pages` 有过一次输入时序 flake，真机是它最该被怀疑的地方）。

### E. 不在本轮

- 300 字/页 × 20 页连续翻页：**没有阅读器 App**，做不了。
- RTL / 阿拉伯连接形：Q1 定为"可选、需实测后决定"，本轮不做。
- 谚文 / 泰文 / CJK 扩展：字体 cmap 里干脆没有（7,582 码点，只有两个 format 4 子表），换字体才谈。
- 热插拔换 TTF（拔卡重插、卡里换了一份）：ADR 0017「未做」里那条。

## 交付物

落 `.scratch/font-ttf/hw/`：`boot.log`、`probe.log`、`nocard.log`、`budget96k.log`、
`shots/hw_<screen>.png`，外加一份 `结果.md`（把上面 A/B/C 的表逐行填上实测值）。

## 风险

1. **端口**：板子是 COM3（USB-Serial/JTAG，今天 13:55 那次就在 COM3）；COM9 是 CH340，别烧错。
2. **烧完无限 panic**：`sd_font_check_fatal` 保持 `false`（默认）。它只在"专门取证这一项"时置
   `true`，取完必须改回来 —— 常开会让缺卡设备进 panic 重启循环，原因看不见、界面也进不去。
3. 改了 `partitions.csv` ⇒ **分区表必须一起烧**（`idf.py flash` 默认会）。
4. 改了 `sdkconfig.defaults` ⇒ 先 `idf.py fullclean`（`platform/esp32/README.md` 第 6 条）。
5. 读卡若确实 > 5 s，**不要在本 issue 里改 `sd_spi_hz`**（10 → 20/40 MHz 是独立决策，要重测卡的稳定性）。
6. 本 issue 顺手改掉三处过期注释（`platform/esp32/README.md` §8、`src/esp32_sd.h`、`src/esp32_sd.cpp`
   写的"默认关、引脚 -1"与现状 `EMBARK_SD_ENABLED 1` + 四脚已填不符）。

## 落地（2026-10-17，只完成"能编出来"这一半）

### 环境根因 → 结论
`export.ps1` 这条路在本机走不通（PATH 上的 python 是 3.14），但 `idf_tools.py export` 这条
路是通的；把它吐出的 PATH 前置后，CMake 的 `tool_version_check` 不再被 Rust 工具链撞死。
上面那段配方一次跑过。

### 固件
- `idf.py -C platform/esp32/project -B build-esp32 build` → **Project build complete**。
- 产物：`embark_esp32.bin` **941,824 B**、`partition-table.bin` 3,072 B、`bootloader.bin` 21,008 B。
- app 分区 0x300000（3,145,728 B）⇒ 固件占 30%，剩 70%。
- 用 `gen_esp32part.PartitionTable.from_binary()` 解分区表 bin：
  `nvs 0x9000/0x6000`、`phy_init 0xf000/0x1000`、`factory 0x10000/0x300000` ——
  **fonts 分区确实不在了**，issue 05 的分区表改动在真机产物上核对通过。
- 注意：`build-esp32/` 是**新 configure** 出来的（旧的 configure 因为工具链版本被拦下过），
  所以这次链接的是 issue 01–07 之后的真实源清单。

### 顺手改的过期注释
`platform/esp32/README.md` §8、`src/esp32_sd.h` 头注释、`src/esp32_sd.cpp` 头注释里
"SD 默认关、四个信号脚是 -1"与现状不符 —— 现状是 `EMBARK_SD_ENABLED 1` + CS IO21 /
MOSI IO17 / MISO IO16 / CLK IO14（issue 25 定的），README §8 也补了"TTF 时代只剩一条字体通路"
的当前口径。

### 还没做（本 issue 的正题）
插卡实跑：A 表七行日志、B 表探针数字、C 表三种降级、D 表 7 屏视觉。全都没跑 ——
本次只把"编不出来"这个前置障碍清掉了。板子今天在 COM3，卡上要有
`fonts/ui_heiti.ttf`（= `app/aidogui_ui/assets/font/ui_heiti.ttf`，3,854,132 B）与 `images/*.bin`。

## 启动占位屏（读字体那 9 秒）

### 为什么有这块屏
`read_ttf()` 在 UI 任务里同步读 3.85 MB（真机 9,296 ms，见 `hw/boot-post.log`），期间 LVGL 一帧也刷不出来
—— 面板全黑，看着像死机。占位屏就是为这几秒存在的：先推一屏上去，再开始阻塞的读。

代码在 `app/common/aidogui_ui_bridge.cpp`（`splash_begin()` … `splash_end()`），**不进 `export/`**：
那目录每次设计器导出都会被清空重生，而且产品根本不调 `aidogui_init()`；进度源（读了多少字节、建了几个字号）
本来就在桥里，放桥里最省事。

### 四代
| 版本 | 形态 | 结论 |
| --- | --- | --- |
| v1 | `EMBARK` + 进度条 + 百分比，纯 ASCII | 能用，但只有一条进度，信息太薄 |
| v2 | 加硬件状态行（分辨率 / PSRAM 池 / 卡根 / 字号），全英文 | PIL 量宽发现 `esp32 240x320 FONT POOL 192 KB` 一行 229 px 会贴边 ⇒ 拆行 |
| v3 | **开机自检（POST）风格**：`[OK]/[..]/[!!]/[  ]` 逐行打勾 + 整行配色 + 右下角 `t=<lv_tick> ms` | 宿主 5 行（显示/字库区/卡/字体/UI），真机 5 行 |
| v4 | 自检行改成**由平台注入、每秒翻绿一行**（`set_boot_checks`），6 行硬件自检 + fonts/ui 两行 + `t=<elapsed> ms` | **当前版本**（用户 2026-10-17：「给别的硬件自检也加个 1s 的进度一项一项的 OK」） |

### v3 的口径（重要）
用户原话：「占位屏本来就是假的，可以除了假装字体加载可以展示别的硬件加载啊，就像Linux的开机自检一样，
我只是要这个效果，当然如果真的有问题就在屏幕上显示吧。」

落地的选择是 **形式像自检、内容全真**：五行分别是显示分辨率、字库区容量、卡挂载点、字体进度、UI 就绪，
全部来自此刻真知道的值（`lv_disp_get_hor_res` / `font_alloc_pool_budget_bytes()` / `aidogui_res_fs_root()` /
`read_ttf()` 的读入字节数 / `load_ttf_fonts()` 的返回值）。真出问题就打红：无卡 `[!!] sd card none`、
卡里没字体 `[!!] fonts not found`、一个字号都没建出来 `[!!] fonts built-in`。
没编任何一行假 `[OK]` —— 一块会用红字报警的自检屏，比一块永远绿的假屏有用。
要多几行（触摸 / PSRAM / RTC 之类）就多一个注入口：那些事实只有 `main.cpp` 知道，桥里编不出来。

### 布局（240×320，top-left 坐标；v4）
标题 `EMBARK <版本>` 居中 y=44、平台行 `esp32 240x320` 居中 y=66；自检行左对齐 x=28、
y=96/116/136/156/176/196（6 行，行距 20 px）；随后 `fonts` y=216、`ui` y=236；
进度条 184×6 @ (28,268)；右下角 `t=<elapsed> ms` 贴右 x=-28 y=290。
宿主没有注入时退回桥自知的 3 行（display / font pool / sd card），进度条落 y=208、时钟 y=230。
最宽一行 `[OK] sdcard SD16G 3504 MB` 量宽 187 px（+28 左边距 = 215 px），右余 25 px。

### v4：一秒翻一行是怎么做到「不花时间」的
没有 `sleep`。这 6 行**全部在 `read_ttf()` 那 9.3 秒里翻绿** —— `read_ttf()` 每 256 KB 回一次
`splash_fonts_progress()`，回调里顺手调 `splash_reveal_checks(false)`，把「到点」的行从灰色 `[  ]`
改成 `[OK]`（或失败时 `[!!]`）。所以占位屏的节奏是读卡进度**免费**带出来的，启动时间一分没多。
`splash_fonts_building()` / `ready()` / `failed()` 里再 `splash_reveal_checks(true)` 立刻全部落定。

注入面是一行 C 结构体（`app/common/aidogui_ui_bridge.h`）：
`struct AidoguiBootCheck { const char* label; const char* detail; bool ok; };`，最多 6 行，
`label`/`detail` 必须 ASCII（那会儿只有内置 184 字形，中文会方框），两个指针要活到 `init` 结束。
实际注入在 `platform/esp32/project/main/main.cpp`：flash / psram / display / touch / sdcard / font pool，
值全部现场查（`esp_flash_get_size`、`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`、
`hal.display().info()`、`hal.sd().info()`、`font_alloc_pool_budget_bytes()`）。
`touch` 那行是固定 `[OK] cst328`：`hal.init()` 失败会直接 fatal，能走到 `ui_main()` 就说明触摸总线通了。
桥里编不出来的事实（板上有几颗灯、RTC 有没有电池……）以后照这个口子加，不用改桥。

### 坑：LVGL 的 tick 在阻塞期是冻住的
第一版用 `lv_tick_get()` 算节奏 ⇒ 真机 6 行**全在同一毫秒**一起翻绿。
根因：`platform/common/lvgl_port.cpp:103` 的 `LvglPort::tick()` 读 `hal::ITime::now_ms()` 求差喂
`lv_tick_inc()`，只在 UI 循环每帧调一次；读卡那 9 秒没有帧 ⇒ `lv_tick` 恒 0。
解法：`set_boot_checks()` 多收一个毫秒时钟回调（`AidoguiBootClock`），`main.cpp` 传
`esp_timer_get_time() / 1000`；没传就退回 `lv_tick_get()`（宿主够用）。修好后实测
`row0@60 / row1@1323 / row2@2580 / row3@3214 / row4@4470 / row5@5106 ms` —— 间隔 0.63~1.26 s，
不是整齐的 1000 ms，因为翻绿只能发生在 256 KB 读块边界上（读块约 580 ms 一个）。观感是「一项一项」的。

### 为什么全英文
这块屏出现时 TTF 还没进内存，能用的只有 LVGL 默认字体 = flash 里的静态子集 `embark_zh_14`（184 字形）。
中文只覆盖现有界面的 84 个字，新文案必然方框。所以文案常量由 `static_assert(ascii_only(...))` 钉死，
谁加了中文直接编不过。

### 证据
- 宿主布局预览：`.scratch/font-ttf/shots/post_mock2.png`（PIL 按同一份 `ui_heiti.ttf` 14 px、同一套坐标画的，
  用于在烧板前抓「会不会贴边/折行」，行长最大 188 px ≤ 212 px 安全区）。
- 真机：`hw/boot-post.log`（2026-10-17 22:1x 那次烧录）。字体加载用时 **9,296 ms**（有占位屏刷新后仍与
  无屏时的 9,254 ms 同量级，15 次进度刷新合计只多几十毫秒）；7/7 已加载；`[AUTODEBUG_PASS] SD_FONT_OK`；
  启动无 panic、无重启循环；心跳帧耗时峰值 112 ms（splash 退场后第一帧要冷栅格化整个 launcher，属预期）。
- 固件 `embark_esp32.bin`：v3 **946,128 B**（加占位屏后 +4,304 B）→ v4 **948,096 B**（+1,968 B），占 app 分区 30%。
- v4 真机证据：`hw/boot-checks-final.log`（`启动自检：注入 6 行` / `字体加载用时 9369 ms` / `字体 7/7 已加载` /
  `[AUTODEBUG_PASS] SD_FONT_OK` / 无 panic）；节奏证据 `hw/boot-cadence2.log`（六行各自的毫秒数）。
- v4 预览：`shots/post_mock3.png`（中途：前 4 行绿、sdcard 灰 `[..]`、font pool 灰 `[  ]`、fonts 62%）、
  `shots/post_mock4.png`（全绿）。
- 视觉验收：2026-10-17 用户上电看过前 9 秒，**验收通过**（颜色/间距/节奏无异议）。宿主的 PIL 预览仍留在
  `shots/post_mock3.png`（中途）与 `shots/post_mock4.png`（全绿），仅用于烧板前量宽防贴边。
