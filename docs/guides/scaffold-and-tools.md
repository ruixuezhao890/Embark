# 脚手架与工具脚本

> **这一层回答"仓库里那几个脚本是干什么的、什么时候该用、改它们要注意什么"**。
> 只想尽快跑起来 → [quickstart.md](quickstart.md)；想理解生成的代码长什么样 → [new-app-guide.md](new-app-guide.md)。

---

## 0. 一览

| 脚本 | 作用 | 何时跑 | 会写什么 |
| --- | --- | --- | --- |
| `tools/scaffold_app.py` | 生成一个带 EEZ 界面的 **App 薄壳** | 加新 App 时 | `app/<名>/` 两个源文件 + `eez_vars.txt`；追加 `app/CMakeLists.txt` 与 `platform/host/ui_demo.cpp` 各一行 |
| `tools/scaffold_app_test.py` | 上面那个脚本的**自测** | 改了 scaffold 之后 | 只写系统临时目录，退出码 0 = 全过 |
| `tools/font/gen_font.mjs` | 生成 **LVGL 静态子集字库** | 界面要加汉字时 | `assets/fonts/embark_zh_14.c` + `tools/font/embark_zh_14.json` |

三个脚本都遵守同一条纪律：**默认不写盘 / 可重复跑 / 找锚点失败就报错退出**，绝不"尽力而为"地半改文件。

---

## 1. `scaffold_app.py`：一键生成 App 薄壳

### 1.1 两种入口

**交互菜单**（推荐，不需要记参数）：

```sh
python tools/scaffold_app.py
```

菜单里选 `1) 新建 App` 或 `2) 给已有 App 追加变量`，全程问答；**每一步都先打印蓝图，最后才问"确认落地？[y/N]"**。

**命令行**（适合脚本化 / 可复制）：

```sh
python tools/scaffold_app.py my --title "我的" --var visit_count:int          # 只预览
python tools/scaffold_app.py my --title "我的" --var visit_count:int --apply  # 落地
python tools/scaffold_app.py my --add-var speed:float --apply                 # 给已有 App 加变量
python tools/scaffold_app.py my --vars-check build/include/embark_eez_vars.h  # 核对构建期变量表
```

| 参数 | 说明 |
| --- | --- |
| `app_name`（位置参数） | App 名，须匹配 `^[a-z][a-z0-9_]*$`，且**不能以 `_sub` 结尾**（那是 EEZ 子页的保留后缀） |
| `--title` | 中文标题（菜单/元数据用），默认"未命名" |
| `--var 字段:类型` | 声明一个 Flow 全局变量骨架，可重复；类型 `int` / `float` / `bool` / `string`（缺省 `int`） |
| `--add-var` | 给**已有** App 增量追加变量（默认预览，配 `--apply` 落地） |
| `--apply` | 真正写盘；不加就是 dry-run |
| `--repo` | 仓库根（默认脚本所在仓库；自测用） |
| `--vars-check FILE` | 核对构建期变量表，见 §1.4 |

### 1.2 一次做完四件事

1. 新建 `app/<名>/<名>_app.{h,cpp}` —— 薄壳模板（含 `name()` / `title()` / `settings()` / 八个钩子的骨架）
2. 新建 `app/<名>/eez_vars.txt` —— **EEZ Studio 侧变量声明的粘贴清单**（见 §1.5）
3. 追加一行到 `app/CMakeLists.txt` 的 `embark_demo_apps` 源列表（锚点：源列表尾行 `common/eez_ui_nav.cpp)`）
4. 追加一项到 `platform/host/ui_demo.cpp` 的 `EMBARK_APP_TABLE`（插在 `LauncherApp` 之后，**首位必须保持启动器**）

> **只注册进 ui_demo.cpp**：仓库里还有另外三张 App 表（`platform/host/user_main.cpp`、`platform/host/ui_tour.cpp`、
> `platform/esp32/project/main/main.cpp`），脚本**不碰**它们——那三处按需手工加。
> 想让自己的程序当入口，照 `user_main.cpp` 改（它的文件头写了"只动三处"）。

### 1.3 幂等与安全

- **默认 dry-run**：不加 `--apply` 只打印蓝图与将改的行，一个字节都不写。
- **幂等**：源文件已存在且内容与模板**完全一致** → 打印"已是最新（幂等跳过）"；
  **内容不一致 → 报错退出、不覆盖**（你手写的业务代码不会被模板冲掉）。
- **锚点找不到就 `die`**：`app/CMakeLists.txt` 里找不到 `common/eez_ui_nav.cpp)`、
  或 `ui_demo.cpp` 里找不到 `EMBARK_APP_TABLE` 原文 → 报错退出并提示"文件结构变了，手工处理"。
- **保留名**：`launcher` / `clock` / `settings` / `common` 菜单模式直接拒绝；同名目录已存在也拒绝。
- 菜单模式的**后台策略**只有三档：`1) suspend`（默认，界面类 App 用它就够）、`2) tick 100ms`、`3) tick 500ms`。
  `own_task` 属高级场景（要配栈深与优先级），菜单**故意不收录**——手改 `settings()` 即可，
  判据见 [../concepts/messages-and-background.md](../concepts/messages-and-background.md) §"怎么选"。

### 1.4 变量加错了怎么办

- **不用重新生成**，也不会动你的业务代码：
  `python tools/scaffold_app.py my --add-var count:int --add-var active:bool --apply`
  新变量插到已有变量之后、`eez_ui_bridge_tick()` 之前；**已在的变量自动跳过**；`eez_vars.txt` 同步追加。
- 菜单单次最多填 **12** 个变量，但**追加次数不限**。
- **`--vars-check`**：变量表的**构建期**产物是 `build/include/embark_eez_vars.h`，
  核对"我 C++ 里写的名字，构建期到底认了哪些"：

```sh
cmake -S . -B build && cmake --build build
python tools/scaffold_app.py my --vars-check build/include/embark_eez_vars.h
```

### 1.5 脚本到此为止：Studio 侧不可脚本化

`.eez-project` 是 **Studio 的二进制工程**，脚本写不了它。所以画屏、声明 Flow 全局变量、绑控件、
按钮设 SetPage —— 这四步必须**人在 Studio 里做**，脚本只输出 `eez_vars.txt` 当粘贴清单：

```text
# EEZ Studio 粘贴清单（tools/scaffold_app.py 生成）——把下面每行声明为 Flow 全局变量并
# 绑定到控件（docs/guides/new-app-guide.md §2.4-2.5）。声明后重新 cmake -B build：构建日志会
# 打印『EEZ 变量：N 个』，或构建后用 --vars-check build/include/embark_eez_vars.h 核对。
# 建屏时同步设好生命周期（工程已勾 Settings→Build『Screens lifetime support』）：
# 新页 General 里 createAtStart 关、Delete on unload 开；启动屏 launcher 保持常驻。
my_visit_count    # int
```

**名字两边必须一模一样**（C++ 侧 `eez_ui_bridge_set_var_int("my_visit_count", n)` ↔ Studio 侧的 Flow 全局变量名）。
对不上**不崩**，只是那个控件永远不刷新——这正是 `--vars-check` 存在的理由。

### 1.6 改脚本之前先跑自测

```sh
python tools/scaffold_app_test.py      # 退出码 0 = 全过
```

它在系统临时目录里搭一个 **fake repo**（只有 `app/CMakeLists.txt` 与 `platform/host/ui_demo.cpp` 两个锚点文件），
然后跑 dry-run / apply / 幂等 / 变量核对 / 非法输入五组断言。**动了 `scaffold_app.py` 就要跑它**——
真实仓库里跑 apply 会写文件，自测不会。

---

## 2. 构建期代码生成（CMake 的两个模块）

`app/eez_ui/` 下 Studio 导出的屏与变量，**不是手写的清单**，而是构建期从生成代码里解析出来的：

| CMake 模块 | 产物 | 来源 | 构建日志 |
| --- | --- | --- | --- |
| `cmake/embark_eez_screens.cmake` | `build/include/embark_eez_screens.h`（屏表） | 解析 `app/eez_ui/src/ui/`：优先 `screen_names[]`，回退 `ScreensEnum` 且排除 `_SCREEN_ID_FIRST` / `_SCREEN_ID_LAST` 哨兵 | `EEZ 屏表：N 个` |
| `cmake/embark_eez_vars.cmake` | `build/include/embark_eez_vars.h`（变量表） | 同上目录下的 Flow 全局变量声明 | `EEZ 变量：N 个` |

两条纪律：

1. **配置期完成**（`file(GLOB ... CONFIGURE_DEPENDS)`）。改了 EEZ 导出**必须重新配置**：
   `cmake -S . -B build` —— 只 `cmake --build` 是不会重新解析的。
2. **降级而非 `FATAL_ERROR`**：解析不出来就生成占位内容，构建照常通过。
   目的是"**没装 Studio、没导出 UI 的机器也能编译**"，而不是让 CI 因为缺文件变红。

---

## 3. `tools/font/gen_font.mjs`：静态子集字库

界面要用汉字就得自带字库（LVGL 默认的 Montserrat 只有 ASCII）。脚本生成一份**子集**：

```sh
node tools/font/gen_font.mjs      # 在仓库根执行
```

产物两个，都在仓库内、可重复生成、幂等覆盖：

- `assets/fonts/embark_zh_14.c` —— lv_font_conv 生成的 LVGL C 字体（`--lv-font-name embark_zh_14`）
- `tools/font/embark_zh_14.json` —— **覆盖清单**，issue 17"缺字审计"的唯一依据

参数口径（为什么是这些值，脚本头部注释里有完整推导）：

| 参数 | 值 | 理由 |
| --- | --- | --- |
| `--size` / `--bpp` | 14 / 4 | 与 LVGL 内置 `montserrat_14` 同字号同色深；16 位 RGB565 屏上 4bpp 抗锯齿足够 |
| `--format` | `lvgl` | 生成 LVGL 原生 `lv_font_fmt_txt_dsc_t`，不是 bin/dump |
| 压缩 | **默认开启** | 需要宿主 `lv_conf.h` 里 `LV_USE_FONT_COMPRESSED = 1`，否则运行时只打一条 WARN 并返回 `NULL` —— **界面一个字都不显示** |
| `--lv-include` | `lvgl.h` | 本工程开了 `LV_LVGL_H_INCLUDE_SIMPLE`，include 根就是 `third_party/lvgl` 仓库根 |

**源字体**：优先 `C:/Windows/Fonts/NotoSansSC-VF.ttf`（Noto Sans SC 是 **OFL 许可，可随仓库分发**）；
备选 `simhei.ttf` / `Deng.ttf` / `msyh.ttc` 只在首选不可用时启用——它们是 Windows 随附的**专有字体**，
仅本机生成可以，随仓库分发有授权问题。图标字形取自 LVGL 内树自带的
`third_party/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff`（与上游生成内置字体用的是同一份）。

**alpha 提升（后处理）**：lv_font_conv 底层用 opentype.js 光栅化，14px 小字号下 CJK 细笔画只覆盖约 40%
（实测"时"字 alpha 主体 6/15），显示明显偏淡；FreeType 同字号主体约 11/15。
脚本对 4bpp 位图做线性提升 `v' = min(15, round(v*GAIN))`，**只重写 `glyph_bitmap[]` 载荷**，
字形几何（adv_w / box / ofs / kerning / cmap）一概不动，回写后逐字形做
「解码(编码(提升后)) == 提升后」**回环断言**，不满足直接抛错。关掉：`ALPHA_GAIN = 0`。

> **本仓库禁 PowerShell 5.1 的 `Set-Content` / `Out-File` / `>` 重定向改源码**
> （会按 GBK 解码 UTF-8 再写成带 BOM 的文件，中文全变乱码）。
> 这个脚本是 Node，`.c` 由 lv_font_conv 自己写、`.json` 用 `fs.writeFileSync(..., 'utf8')` 写，
> 末尾还会断言两个文件都**没有 BOM**。

---

## 4. 给仓库加一个新脚本时

照现有三个的规矩来，评审时才不会被退：

1. **默认 dry-run**，`--apply` 才写盘；重复跑必须幂等；内容不一致**报错退出**，不要静默覆盖。
2. **锚点找不到就 `die`**，不要"尽力而为"地半改——半改比不改更难查。
3. **自带自测**（`tools/<名>_test.py`，或给脚本加 `--check` 冒烟），能在临时目录跑，退出码 0 = 全过。
4. **输出 UTF-8**：Python 脚本开头 `sys.stdout.reconfigure(encoding="utf-8")`（Windows 控制台）。
5. **不要用 PowerShell 写文件**（见上）。

---

## 5. 相关文档

- 新手路径：[quickstart.md](quickstart.md) → [new-app-guide.md](new-app-guide.md)
- 生成的薄壳里八个钩子该写什么：[../reference/hooks-and-api.md](../reference/hooks-and-api.md) §1
- EEZ 桥 API 全集：[eez-ui-manual.md](eez-ui-manual.md) §8
- Studio 侧操作：[eez-studio-guide.md](eez-studio-guide.md)
- 各种容量上限：[../reference/limits.md](../reference/limits.md)
