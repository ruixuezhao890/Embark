# 工具与脚本

> **这一层回答"仓库里那几个脚本是干什么的、什么时候该用、改它们要注意什么"**。
> 只想尽快跑起来 → [quickstart.md](quickstart.md)；想理解界面代码从哪来 → [new-app-guide.md](new-app-guide.md)。

---

## 0. 一览

| 脚本 | 作用 | 何时跑 | 会写什么 |
| --- | --- | --- | --- |
| `tools/aidogui/export.ps1` | **无头重导出** AiDoGUI 界面代码（不开设计器） | 没开 GUI / CI / 想敲一条命令重生成 | 清空并重建 `app/aidogui_ui/export/` |
| `tools/aidogui/gen_ui.cjs` | 上面那个的**内核**：借 AiDoGUI 自己的 Node 跑它自己的生成器 | 一般不直接调 | 同上 |
| `tools/aidogui/html_to_aid.mjs` | **HTML 草稿 → 屏**：无头浏览器量真坐标，合并进 `screen.aid` 的指定屏（详见 [html-to-screen.md](html-to-screen.md)） | 想不打开设计器改屏 / 只想写语义不写坐标 | 默认不写盘；`--apply` 只写 `screens[<屏>].widgets`，没变的控件原样保留 |
| `tools/aidogui/aid_probe.js` | 上面那个注入浏览器的**量测探针**（在页面里收集 rect + 计算样式） | 不直接调 | 无（拷进临时目录跑） || `tools/aidogui/gen_widget_doc.mjs` | **控件对照表**：把 `screen.aid` 的控件与 `app/`、`platform/host/` 里的调用点 join 起来 → `docs/reference/ui-widgets.md`（详见 [reference/ui-widgets.md](../reference/ui-widgets.md)） | 改完屏（尤其控件改名 / 删控件）后 | 只写 `docs/reference/ui-widgets.md`；`--check` 只比对不写 |
| `tools/font/gen_font.mjs` | 生成 **LVGL 静态子集字库** `embark_zh_14`（内置默认字体） | 内置字库要加字时 | `assets/fonts/embark_zh_14.c` + `tools/font/embark_zh_14.json` |
| `tools/font/audit_glyphs.mjs` | **缺字审计**（CI 门禁） | 加/改界面文案后 | 只读；缺字打印码点 + `文件:行号` 并 `exit 1` |

七个脚本都遵守同一条纪律：**默认不写盘 / 可重复跑 / 找锚点失败就报错退出**，绝不"尽力而为"地半改文件。

**加 App 没有一键脚本**——照 `app/music/` 抄一份薄壳，见 [new-app-guide.md](new-app-guide.md) §2。

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

**退出码**：`0` = 正常；`1` = 后端起不来或用法错误；`2` = **验收失败**（日志写明哪一步）。

> **带合成点击/手势的验收必须给 `--delay 40`（或更大）**：LVGL 约每 30 ms 读一次输入；
> 帧跑得比它快时，一帧里推的"按下 → 移动 → 抬起"会被采样进同一次读取 —— 同一份脚本
> 会一趟过一趟不过（点击晚十几帧生效、拖动看着没吸附）。`--ui` 没有合成输入，不用管。

`embark_host_tour` 是另一条线：一条用例走完进程入口到关窗，末尾打一张 **17 项自检清单**
（✓/✗ + 实测值），全过退出码 `0`、有失败退出码 `2`。它只有 `--scale` / `--delay` / `--frames` /
`--screenshot` / `--help`。

---

## 2. `tools/aidogui/export.ps1`：无头重导出界面

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

## 3. 字体：`gen_font.mjs` + `audit_glyphs.mjs`

仓库里同时有**两套字体**，别搞混：

| 字体 | 谁用 | 从哪来 |
| --- | --- | --- |
| `embark_zh_14`（静态子集，184 字形） | LVGL **默认字体**：没显式设字体的文本 | `tools/font/gen_font.mjs` 构建期生成、随仓库入库 |
| 卡上的整份 TTF `ui_heiti.ttf` | AiDoGUI 界面的每个字号槽（运行期栅格化） | 卡上 `fonts/`（宿主是 `app/aidogui_ui/export/`）；[ADR 0017](../adr/0017-font-ttf-from-sd.md) |

### 3.1 生成内置子集字库

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

### 3.2 缺字审计（CI 门禁）

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

## 4. 给仓库加一个新脚本时

照现有四个的规矩来，评审时才不会被退：

1. **默认 dry-run**，`--apply` 才写盘；重复跑必须幂等；内容不一致**报错退出**，不要静默覆盖。
2. **锚点找不到就 `die`**，不要"尽力而为"地半改——半改比不改更难查。
3. **自带自测**（`tools/<名>_test.py`，或给脚本加 `--check` / `--selftest` 冒烟），能在临时目录跑，退出码 0 = 全过。
4. **输出 UTF-8**：Python 脚本开头 `sys.stdout.reconfigure(encoding="utf-8")`（Windows 控制台）。
5. **不要用 PowerShell 写文件**（见上）。

---

## 5. 相关文档

- 新手路径：[quickstart.md](quickstart.md) → [new-app-guide.md](new-app-guide.md)
- 界面代码从哪来、怎么改：[../../app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)
- 八个钩子该写什么：[../reference/hooks-and-api.md](../reference/hooks-and-api.md) §1
- 界面薄桥 API：[../reference/hooks-and-api.md](../reference/hooks-and-api.md) §5
- 各种容量上限：[../reference/limits.md](../reference/limits.md)
