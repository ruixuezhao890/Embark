# 工具与脚本

> **这一层回答"仓库里那几个脚本是干什么的、什么时候该用、改它们要注意什么"**。
> 只想尽快跑起来 → [getting-started.md](getting-started.md)；想理解界面代码从哪来 → [new-app-guide.md](new-app-guide.md)。

---

## 0. 一览

| 脚本 | 作用 | 何时跑 | 会写什么 |
| --- | --- | --- | --- |
| `tools/new_app.mjs` | **新 App 薄壳**：读 `app/music/` 当模板 → 生成 `app/<名>/<名>_app.{h,cpp}`，登记 7 行（宿主 4 + 真机 3；`--no-device` 只做宿主），并往 `.aid` 补一张同名占位屏（见 [new-app-guide.md](new-app-guide.md) §2） | 想加一个 App 时 | 默认不写盘；`--apply` 才写 2 个新文件 + 改 3 个既有文件 + 改 1 份 `.aid`（`--aid` 指定，缺省 `app/aidogui_ui/screen.aid`）；`--delete` 反向撤 |
| `tools/aidogui/dev.ps1` | **日常开发循环**：导出 → 构建 → 起 `embark_host_ui --screen <屏> --watch`，盯源码变了就重建重启（见 §2） | 想「改一行就看见结果」的时候 | 同 export.ps1 + 构建产物 |
| `tools/appconsole/` | **App 管理台（本地网页控制台）**：看每个 App 的登记齐了没有、加 App / 退 App，顺手导出屏 + 构建宿主（见 §7） | 想用鼠标代替「敲 new_app + 导出 + 构建」 | 起一个只监听 `127.0.0.1` 的小服务；点了才动文件（先给计划）；不写状态文件 |
| `tools/aidogui/export.ps1` | **无头重导出** AiDoGUI 界面代码（不开设计器） | 没开 GUI / CI / 想敲一条命令重生成 | 清空并重建 `app/aidogui_ui/export/` |
| `tools/aidogui/gen_ui.cjs` | 上面那个的**内核**：借 AiDoGUI 自己的 Node 跑它自己的生成器 | 一般不直接调 | 同上 |
| `tools/aidogui/html_to_aid.mjs` | **HTML 草稿 → 屏**：无头浏览器量真坐标，合并进 `screen.aid` 的指定屏（详见 [html-to-screen.md](html-to-screen.md)） | 想不打开设计器改屏 / 只想写语义不写坐标 | 默认不写盘；`--apply` 只写 `screens[<屏>].widgets`，没变的控件原样保留 |
| `tools/aidogui/aid_probe.js` | 上面那个注入浏览器的**量测探针**（在页面里收集 rect + 计算样式） | 不直接调 | 无（拷进临时目录跑） |
| `tools/aidogui/gen_widget_doc.mjs` | **控件对照表**：把 `screen.aid` 的控件与 `app/`、`platform/host/` 里的调用点 join 起来 → `docs/reference/ui-widgets.md`（详见 [reference/ui-widgets.md](../reference/ui-widgets.md)） | 改完屏（尤其控件改名 / 删控件）后 | 只写 `docs/reference/ui-widgets.md`；`--check` 只比对不写 |
| `tools/esp/idf.ps1` | **真机构建 / 烧录的壳**：注入 ESP-IDF 环境（`IDF_PATH`、Python venv、工具的 PATH）后转调 `idf.py`（见 §7.3） | 命令行烧板子（App 管理台不管烧录） | 只写 `build-esp32/` 与板子 |
| `tools/esp/serial.ps1` | **纯文本串口监视**：把板子的串口日志按行打到 stdout（`idf.py monitor` 会喷 ANSI 又等键盘） | 命令行看真机日志 | 无 |
| `tools/font/gen_font.mjs` | 生成 **LVGL 静态子集字库** `embark_zh_14`（内置默认字体） | 内置字库要加字时 | `assets/fonts/embark_zh_14.c` + `tools/font/embark_zh_14.json` |
| `tools/font/audit_glyphs.mjs` | **缺字审计**（CI 门禁） | 加/改界面文案后 | 只读；缺字打印码点 + `文件:行号` 并 `exit 1` |
| `tools/check_docs.mjs` | **文档过期自检**：文档里的路径 / `embark_host_ui` 开关 / 脚本参数 / 锚注释还对不对得上（见 §6） | 改了工具、开关或文档之后（[AGENTS.md](../../AGENTS.md) 也这么要求） | 只读；有过期就打印 `文件:行号` 并 `exit 1` |

十二个脚本（再加上 `tools/appconsole/` 那个网页控制台）都遵守同一条纪律：**默认不写盘 / 可重复跑 / 找锚点失败就报错退出**，绝不"尽力而为"地半改文件。

**加 App 有脚本了**：`node tools/new_app.mjs my_app --title 我的应用 --apply`（先不带 `--apply` 跑一次，只打印计划）。
它**不内嵌模板**——运行期读 `app/music/music_app.{h,cpp}` 做替换，模板只有一份真相；
`node tools/new_app.mjs --self-check` 检查那份模板与 6 处登记锚点还在不在。详见 [new-app-guide.md](new-app-guide.md) §2。

---

## 1. 验收开关与退出码（`embark_host_ui`）

界面链路的自动验收全在这一个可执行文件里，每个开关是一段**脚本化故事**（合成点击 / 手势 + 断言 + 中文证据行）。

| 开关 | 故事 | 默认帧数 |
| --- | --- | --- |
| `--ui` | 字体 6/6 + 屏表/控件表自检 + 切走即释放/回来即重建 | 40 |
| `--ui-nav` | 点磁贴进 clock → 点返回回 launcher → 点时钟卡再进 clock → 点「>」「<」翻月并校验 | 110 |
| `--ui-weather` | 点天气卡进天气屏 → 5 天逐字段对桩 → 「>」切城市 → 返回后主屏天气卡跟着换 | 110 |
| `--ui-launcher-pages` | 磁贴/页码点按注册表生成 → 拖到第 2 页 → 进相册 → 回主屏 → 进设置 → 「亮度 +1」→ 回主屏 | 132 |
| `--click [X,Y]` | 点时钟卡切 clock（帧 20）→ 帧 70 在 clock 屏点 (X,Y)（默认返回键中心 = 回启动器） | 90 |
| `--drag X1,Y1,X2,Y2` | 合成拖动冒烟（帧 15 按下 → 逐帧插值 → 帧 40 抬起） | 80 |
| `--launch` | 完整故事：`request_switch('clock')` → 点屏按钮往返 → `bump_brightness` 消息广播 | 200 |
| `--imu-log` | 每 500 ms 打一行姿态读数（issue 08），静止时 `az ≈ -1.00` | 不涉及 |
| `--imu-shake` | 把合成姿态换成正弦（周期 2 s、幅度 0.5 g），配 `--imu-log` 看变化 | 不涉及 |
| `--audio-test` | 音频服务验收（issue 09）：播 `chime.wav`，逐项跑 播 → 暂停 → 继续 → 跳转 → 停止 → 音量边界，打 `✔ / ✘`；**不需要屏幕，建 LVGL 之前就退出** | 不涉及 |
| `--net-test` | 网络服务验收（issue 10）：GET `http://example.com/`，验状态机、body 缓冲（`no_space`）、断连后 `not_ready`；**需要联网**，不联网会如实失败；不需要屏幕 | 不涉及 |

辅助开关：`--frames N` / `--scale S` / `--delay MS` / `--screenshot FILE` / `--shot-frame N FILE` / `--quit-at N` / `--help`。

开发用开关：`--screen NAME`（启动直接进这块屏，屏名 == App 名）/ `--watch`（每 200 ms 看一次 `export/`，变了就把当前屏卸了重装，进程不重启）/ `--shot-live FILE [--shot-live-ms N]`（每 N 毫秒把当前画面覆盖写进 FILE，默认 500 ms；进程照跑、窗口照点，录演示能用）。前两个配起来就是 `dev.ps1` 里跑的那条命令，见 §2。

> `--shot-live` 只往**仓库内**的路径写稳妥：写仓库外的绝对路径会失败（原因见 [pitfalls.md](../reference/pitfalls.md)）。

**退出码**：`0` = 正常；`1` = 后端起不来或用法错误；`2` = **验收失败**（日志写明哪一步）。

> **带合成点击/手势的验收必须给 `--delay 40`（或更大）**：LVGL 约每 30 ms 读一次输入；
> 帧跑得比它快时，一帧里推的"按下 → 移动 → 抬起"会被采样进同一次读取 —— 同一份脚本
> 会一趟过一趟不过（点击晚十几帧生效、拖动看着没吸附）。`--ui` 没有合成输入，不用管。

`embark_host_tour` 是另一条线：一条用例走完进程入口到关窗，末尾打一张 **17 项自检清单**
（✓/✗ + 实测值），全过退出码 `0`、有失败退出码 `2`。它只有 `--scale` / `--delay` / `--frames` /
`--screenshot` / `--help`。

---

## 2. 日常循环：`tools/aidogui/dev.ps1`

```powershell
pwsh -NoProfile -File tools/aidogui/dev.ps1 -Screen clock   # 导出 → 构建 → 起窗口（clock 屏）
pwsh -NoProfile -File tools/aidogui/dev.ps1 -SkipExport     # 只改了 C++，省掉一次导出
pwsh -NoProfile -File tools/aidogui/dev.ps1 -NoRun          # 只导出 + 构建，不起窗口（CI）
```

它按顺序做三件事：`export.ps1` → `cmake -S . -B build` + `cmake --build build --target embark_host_ui`
→ 起 `embark_host_ui --screen <屏> --watch`。跑起来之后它还在盯着 `src/ app/ include/ platform/ cmake/`
的源文件：一发现变了就**停窗口 → 重新构建 → 再起一个**（进程级重启；Q3 已明确排除 C++ 热重载）。

**两件事别混，否则会以为热重载坏了**：

| 你改了什么 | 谁负责让它生效 |
| --- | --- |
| `export/` 里的东西（图片 BIN 这类运行期才从 `S:` 盘读的） | `--watch`：进程内卸掉当前屏再装上，**窗口不重启** |
| `.aid` 的**文案与几何**（导出成 C 代码、编进二进制） | `dev.ps1` 的第 1、2 步：必须重新导出 + 重新构建 |
| `.cpp` / `.h` | `dev.ps1` 的重启循环：停窗口 → 构建 → 再起 |

热重载为什么必须「先卸再装」（`app/common/aidogui_ui_bridge.cpp` 的 `load_entry`）：
桥对「已经是当前屏」直接 `return true`，不重建。所以 `--watch` 走的是
`aidogui_ui_bridge_unload_current()` → `aidogui_ui_bridge_load_screen(同一块屏)`：卸的那步把显示器
交给空屏（不留悬空 `act_scr`），装的那步观察者会重挂点击绑定、并让 App 把值重推一遍。

> `--watch` 缺省关闭，`--screen` 缺省是 `launcher`：不带这两个开关时 `embark_host_ui` 的行为与以前完全一样，
> 四条验收故事不受影响。

---
## 3. `tools/aidogui/export.ps1`：无头重导出界面

主路径是 GUI（在 AiDoGUI Designer 里改屏 → 导出向导 → 输出目录选 `app/aidogui_ui/export`）。
本脚本是**可选路径**：给"没开设计器 / CI / 想敲一条命令重生成"用，走同一个生成器、写同一个目录，产物完全一样。

```powershell
pwsh -NoProfile -File tools/aidogui/export.ps1
pwsh -NoProfile -File tools/aidogui/export.ps1 -Project app/aidogui_ui/screen.aid -OutDir app/aidogui_ui/export
```

它做三件事：**找 AiDoGUI 安装目录** → 调 `gen_ui.cjs` 跑 AiDoGUI 自己的代码生成器（读 `screen.aid`，BIN 格式）
→ **清空并重建 `app/aidogui_ui/export/`**（只允许这个目录，防止删错）。

- **安装目录三级查找**：参数 `-AiDoGuiHome` → 环境变量 `AIDOGUI_HOME` → 仓库根 `.aidogui-home` → `tools/aidogui/home.txt`（本机私有，已 gitignore）。换机器改最后一处。
- **字体不在产物里**（[ADR 0017](../adr/0017-font-ttf-from-sd.md)）：字体是卡上的整份 TTF、运行期栅格化。设计器只会出 BIN|C 两种字体产物，两种都不是我们要的 —— 脚本导完就删 `font/` 并把生成代码里的字体初始化打桩。
- **屏表/控件表/字体表不在这里生成**：`cmake/embark_aidogui_tables.cmake` 在 **CMake 配置期**解析 `export/screens/ui_*.h` 与 `export/ui.c`，写到构建目录的 `include/embark_aidogui_ui_tables.h`。

> 改了界面并重导出后，**构建会自动重跑配置**（`CONFIGURE_DEPENDS`）；但如果是手工改了导出文件又只 `cmake --build`，保险起见跑一次 `cmake -S . -B build`。

不想打开设计器、只想写一段 HTML 把屏改出来 → [html-to-screen.md](html-to-screen.md)。

---

## 4. 字体：`gen_font.mjs` + `audit_glyphs.mjs`

仓库里同时有**两套字体**，别搞混：

| 字体 | 谁用 | 从哪来 |
| --- | --- | --- |
| `embark_zh_14`（静态子集，184 字形） | LVGL **默认字体**：没显式设字体的文本 | `tools/font/gen_font.mjs` 构建期生成、随仓库入库 |
| 卡上的整份 TTF `ui_heiti.ttf` | AiDoGUI 界面的每个字号槽（运行期栅格化） | 卡上 `fonts/`（宿主是 `app/aidogui_ui/export/`）；[ADR 0017](../adr/0017-font-ttf-from-sd.md) |

### 4.1 生成内置子集字库

```sh
node tools/font/gen_font.mjs      # 在仓库根执行
```

产物两个，都在仓库内、可重复生成、幂等覆盖：`assets/fonts/embark_zh_14.c`（lv_font_conv 生成的 LVGL C 字体）
与 `tools/font/embark_zh_14.json`（覆盖清单，**存档**：缺字审计已改为对照 TTF 的 cmap，不再读它）。

**它怎么被用上**：`config/lv_conf.h` 里 `LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(embark_zh_14)` +
`LV_FONT_DEFAULT &embark_zh_14` 把它接成 LVGL 的默认字体。因为默认字体被 `liblvgl.a` 里的
`lv_theme.c.obj` / `lv_draw_label.c.obj` 引用，而 GNU ld 只在「扫到某个归档时该符号仍未定义」才把成员拉进来、
库顺序不由我们决定，所以字库归档必须**整体收进链接**：宿主 `-Wl,--whole-archive embark_zh_font -Wl,--no-whole-archive`
（`platform/common/CMakeLists.txt`），真机 `idf_component_register(... WHOLE_ARCHIVE)`。少了这一步的现象是
**编译全过、字库根本没进镜像**（map 里搜不到 `embark_zh_14`），不是报错。

参数口径（为什么是这些值，脚本头部注释里有完整推导）：

| 参数 | 值 | 理由 |
| --- | --- | --- |
| `--size` / `--bpp` | 14 / 4 | 与 LVGL 内置 `montserrat_14` 同字号同色深；16 位 RGB565 屏上 4bpp 抗锯齿足够 |
| `--format` | `lvgl` | 生成 LVGL 原生 `lv_font_fmt_txt_dsc_t`，不是 bin/dump |
| 压缩 | **默认开启** | 需要 `lv_conf.h` 里 `LV_USE_FONT_COMPRESSED = 1`，否则运行时只打一条 WARN 并返回 `NULL` —— **界面一个字都不显示** |
| `--lv-include` | `lvgl.h` | 本工程开了 `LV_LVGL_H_INCLUDE_SIMPLE`，include 根就是 `third_party/lvgl` 仓库根 |

**源字体**：优先 `C:/Windows/Fonts/NotoSansSC-VF.ttf`（Noto Sans SC 是 **OFL 许可，可随仓库分发**）；
备选 `simhei.ttf` / `Deng.ttf` / `msyh.ttc` 只在首选不可用时启用 —— 它们是 Windows 随附的**专有字体**，
仅本机生成可以，随仓库分发有授权问题。图标字形取自 LVGL 内树自带的
`third_party/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff`。

**alpha 提升（后处理）**：lv_font_conv 底层用 opentype.js 光栅化，14px 小字号下 CJK 细笔画只覆盖约 40%
（实测"时"字 alpha 主体 6/15），显示明显偏淡。脚本对 4bpp 位图做线性提升 `v' = min(15, round(v*GAIN))`，
**只重写 `glyph_bitmap[]` 载荷**，字形几何（adv_w / box / ofs / kerning / cmap）一概不动，回写后逐字形做
「解码(编码(提升后)) == 提升后」**回环断言**，不满足直接抛错。关掉：`ALPHA_GAIN = 0`。

> **本仓库禁 PowerShell 5.1 的 `Set-Content` / `Out-File` / `>` 重定向改源码**
> （会按 GBK 解码 UTF-8 再写成带 BOM 的文件，中文全变乱码）。
> 这个脚本是 Node，`.c` 由 lv_font_conv 自己写、`.json` 用 `fs.writeFileSync(..., 'utf8')` 写，
> 末尾还会断言两个文件都**没有 BOM**。

### 4.2 缺字审计（CI 门禁）

```sh
node tools/font/audit_glyphs.mjs                    # 审计 app/ src/ platform/，字体 cmap 里没有的字符就 exit 1
node tools/font/audit_glyphs.mjs --print-required   # 打印当前界面文案用到的全部字符（换字体时核对面）
node tools/font/audit_glyphs.mjs --selftest         # 临时目录里自造缺字夹具，验证报错路径与行号
node tools/font/audit_glyphs.mjs --ttf 别的字体.ttf --include-symbols --roots=app,src
```

对照物是**卡上那份 TTF**（`app/aidogui_ui/assets/font/ui_heiti.ttf`）：脚本自己解它的 cmap（format 4 / 12，零依赖），
不读任何清单。扫描所有会把字符串送上屏的 setter（`lv_label_set_text` / `_fmt` / `_static`、`lv_checkbox_set_text`、
`lv_dropdown_set_options`、`lv_roller_set_options`、`lv_btnmatrix_set_map`、`lv_table_set_cell_value`…，白名单是脚本里的
`UI_TEXT_SETTERS`），缺字时打印字符（含 U+ 码点）+ `文件:行号` 并 `exit 1`；名字像 setter 却不在白名单里的调用
也会被点名（`SUSPECT_PATTERNS`），免得哪天换个写法把字符串绕过去还全绿。LVGL 图标区 `U+F000–U+F8FF` 默认豁免
（内置字形，不进 cmap），`--include-symbols` 可关掉豁免。CI 里是独立的 `font-audit` job（`.github/workflows/ci.yml`）。

工作流：**加了界面文案 → 跑审计**。缺字只有三条出路：换字体 / 改文案 / 给这个 label 显式指定一个含该字的字体
（内置 `embark_zh_14` 只有 184 个字形，指望不上）。

---

## 5. 给仓库加一个新脚本时

照现有脚本的规矩来，评审时才不会被退：

1. **默认 dry-run**，`--apply` 才写盘；重复跑必须幂等；内容不一致**报错退出**，不要静默覆盖。
2. **锚点找不到就 `die`**，不要"尽力而为"地半改——半改比不改更难查。
3. **自带自测**（`tools/<名>_test.py`，或给脚本加 `--check` / `--selftest` 冒烟），能在临时目录跑，退出码 0 = 全过。
4. **输出 UTF-8**：Python 脚本开头 `sys.stdout.reconfigure(encoding="utf-8")`（Windows 控制台）。
5. **不要用 PowerShell 写文件**（见上）。
6. **用 Node 写（`tools/*.mjs`）时**：结果放 `process.exitCode`，别用 `process.exit()` 硬切 —— 管道里的 stdout 会被截断；零依赖，只用 `node:*` 内置模块。

---

## 6. 文档自检：`check_docs.mjs`

改了工具、开关或文档之后跑一次，五类检查：

| # | 查什么 | 例子 |
| --- | --- | --- |
| ① | md 链接指向的文件还在不在 | `[tools](tools.md)` 里的 `tools.md` 被改名了 |
| ② | 反引号里的仓库路径还在不在 | 写着 `tools/old_script.py`，文件早删了 |
| ③ | `embark_host_ui` 的开关是不是真的 | 文档写 `--eez`，那开关随 issue 01 删了 |
| ④ | 脚本参数是不是真的 | 文档写 `dev.ps1 -Fast`，脚本没这个参数 |
| ⑤ | 锚注释还对不对得上 | 写着 `# 新 App 登记锚点`，源文件里那句被改了 |

```sh
node tools/check_docs.mjs            # 扫一遍；有过期就 exit 1
node tools/check_docs.mjs --verbose  # 连「扫了多少文件、多少条检查」一起打
node tools/check_docs.mjs --self-test # 用内存假样本验证五类检查真会报错、正向样本零误报
```

**它有三类东西不查**（都有理由，不是漏洞 —— 改检查逻辑前先看脚本头部注释）：

- `docs/adr/**` 只查链接 —— ADR 是决策史，本来就该提那些已删掉的文件。
- 行里写了 `check_docs:skip` 标记的 —— 那行在讲「这些文件已退役」这类历史。
- `ALLOW_MISSING` 里的路径 —— 构建期生成、本来就不在仓库里（如 `include/embark_aidogui_ui_tables.h`）。

> **误报比漏报更伤**：误报一次，这个脚本就没人跑了。加检查时宁可少一条，也不要把正常写法判成过期。

---

## 7. App 管理台：`tools/appconsole/`（不想敲命令就点按钮）

双击 `tools/appconsole/start.cmd` → 浏览器自动打开 `http://127.0.0.1:7788/`（端口被占会自动往后挪，最多试 12 次）。

它只解决「加一个 App / 退一个 App 要动 6 个地方」这件事，别的都留给命令行：

| 面板上的 | 背后跑的 |
| --- | --- |
| App 一览（名字 / 类名 / 标题 / 图标 / 宿主 4/4 / 真机 3/3 / 屏状态） | `node tools/new_app.mjs --status` |
| 看计划 | `node tools/new_app.mjs <名>`（缺省就是 dry-run：只打印改哪几行、插进去什么片段） |
| 建它 | 同上加 `--apply`，成功后接着导出屏 + 构建宿主 |
| 退掉 | `node tools/new_app.mjs <名> --delete`（先看计划，再手动敲一遍名字确认） |
| 打开设计器 | `AiDoGUIDesigner.exe <当前 .aid>` |
| 导出屏 / 构建宿主 | 导出屏 / `cmake -S . -B build` + `cmake --build build --target embark_host_ui` |

不做（有意）：实时画面、在网页里点画面、串口、烧真机、跑验收故事 —— 那些继续走命令行，见 §1、§7.3 与 [daily-loop.md](daily-loop.md)。

### 7.1 三条要说清的话

1. **它不存第二份知识**：哪几处登记、占位屏长什么样，只写在 `tools/new_app.mjs` 里。面板把它的 `--status --json` / `--json` 结果画出来，落盘也是起子进程跑它 —— 命令行和网页永远同一套行为。
2. **它不重写逻辑**，只是把命令摆成按钮，命令行一个都没少。日志按通道（`new_app` / `export` / `build` / `panel`）标好，哪一步失败就停在哪一步。
3. **一个时刻只跑一个任务**：导出会**先清空 `app/aidogui_ui/export/` 再重新生成**，两个任务撞上就废了，所以有全局锁（有任务在跑时按钮是灰的）。

### 7.2 加 / 退一个 App 都会顺带动 `.aid`

- 加的时候补一张同名占位屏（抄 `album` 的 7 个控件：背景 / 返回键 / 标题 / 占位卡片），屏名 == App 名。
- 退的时候只在「那张屏还是脚本当初建的样子」时才撤 —— 你动过它就留着并提示，要删只能自己去设计器里删。`launcher` 不给删（删了就回不到桌面）。
- 两件事之后面板都会**自动重新导出 + 构建**（屏的 C 代码是导出产物，不导出就找不到 `ui_<app>`）。

### 7.3 真机还在命令行（四条须知）

App 管理台不管烧录。真机构建 / 烧录 / 看日志是三条命令：

```sh
pwsh -NoProfile -File tools/esp/idf.ps1 -Task build                 # 增量编真机固件（1~3 分钟）
pwsh -NoProfile -File tools/esp/idf.ps1 -Task flash -Port COM3      # 编完烧，烧完自动 reset
pwsh -NoProfile -File tools/esp/serial.ps1 -Port COM3               # 纯文本串口日志（Ctrl+C 退出）
```

四条坑（**真机没有画面回传** / **串口是独占的** / **串口里的中文显示成 `???`** /
**本机 COM3 是板子、COM9 是 CH340**）记在 [pitfalls.md](../reference/pitfalls.md)。

## 8. 相关文档

- 新手路径：[getting-started.md](getting-started.md) → [new-app-guide.md](new-app-guide.md)
- 界面代码从哪来、怎么改：[../../app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)
- 八个钩子该写什么：[../reference/hooks-and-api.md](../reference/hooks-and-api.md) §1
- 界面薄桥 API：[../reference/hooks-and-api.md](../reference/hooks-and-api.md) §5
- 各种容量上限：[../reference/limits.md](../reference/limits.md)
