# 17 · 字体服务：静态子集 + 运行时加载接口 + 缺字审计

Status: resolved
Type: feature
Blocked by: —
来源: 用户设计问答（"在单片机上不可能说全部烧录到flash吧？可以从sd卡加载到psram中再进行引用吧？"）
+ Round 3 四项全部采纳推荐（框架服务接口先行 / 本次小集 / lv_font_load 主路线 / 构建期审计挂 CI）

## 目标

两层字库策略落地：静态子集字库（flash，覆盖审计门禁）+ 运行时加载的字库服务（IFileSystem
接口先行、宿主后端可用可测、记账独立于 LVGL 全局堆）；真机 SD 后端挂后续 issue（门槛：HAL 面 +
板子接线确认）。

## 已定契约（设计已敲定，实现按此）

### 静态子集字库
- 管线：lv_font_conv 生成压缩位图 C 数组（LV_USE_FONT_COMPRESSED 方向），编译进 flash；
  本次只收启动器与框架壳实际用到的字符（几十字级），无 CJK 正文字库。
- 覆盖清单：每个静态字库一个清单文件（tools/font/ 下，字符表 + 字号 + 文件名），
  是缺字审计的唯一依据。

### IFileSystem（框架服务接口）
- 接口：open/read/seek/close（字节流语义，返回 Error 码，`not_found` 等复用现有错误码）。
- 宿主后端：读可执行文件旁目录（与持久化同模式），现在就能跑。
- 真机后端：后续 issue（`esp32_bus.h` 明写 `spi_transfer` 如实返回 `unsupported`，
  "等真要挂 SD 卡之类的外设时"先加 HAL 面；板子 SD 接线需用户手册确认）。

### 运行时字库与记账
- 包装 LVGL 原生 `lv_font_load(path)` 与 `lv_font_free()`（`src/font/lv_font_loader.h`，
  8.3.11 自带），字库文件与 lv_font_conv 数据同源打包。
- 记账层：字节预算（默认值实现时定，量级 100 KB）+ 引用计数；重复 load 复用（引用 +1）；
  超预算返回 `no_space`；记账独立于 LVGL 全局堆预算（256 KB 静态池）。
- 真机映射：字库缓冲走 PSRAM 显式分配（heap_caps），记账不变。
- 本次落地范围：接口 + 宿主后端 + 记账 + 一个宿主端到端用例（加载 fixture 字库、渲染 1 个
  字形、free 后账目归零）；App 层 v1 不使用运行时字库。

### 缺字审计（CI 门禁）
- 扫描范围：app/ + 框架壳字符串（src/embark、platform/common 导航壳文案）。
  注（issue 19 落地时补充）：EEZ Studio 生成代码 `app/eez_ui/` 的运行时文案
  （`screens.c` 里的 `lv_label_set_text`）也在此范围——生成文案全 ASCII，
  需与静态子集清单一起过门禁。
- 规则：源码字符串逐字符对照覆盖清单，字库外字符 → 构建失败（明确报字符 + 所在文件行）。
- CI：新增一个 job（复用现有检查 job 或新 job）；audit 脚本失败即红。

## 验收（草案）
- IFileSystem 宿主后端单测：打开/缺失文件（`not_found`）/EOF 语义/关闭幂等。
- 记账单测：load 复用引用计数、超预算 `no_space`、free 归零、错误路径不泄漏。
- 端到端：宿主进程加载 fixture 字库渲染字形成功，账目核对一致。
- 审计：构造含字库外字符的用例 → 脚本报字符与位置；CI 全绿。
- 零分配审计用例仍全绿（新增部分无堆分配；宿主后端文件 IO 属平台线，与既有宿主实现口径一致）。

## 备注
- FreeType 推迟：`src/extra/libs/freetype` 内树可用（`LV_USE_FREETYPE` 门控、缓存默认
  16 KB），出现任意 Unicode 文本需求（音乐/文件浏览）再开；`config/lv_conf.h` 保持关闭，
  文件系统驱动亦然（运行时加载在宿主落地时再开，宿主路径自持）。
- 与 issue 16 依赖：启动器标题与壳文案进子集字库并过审计；LV_SYMBOL 图标码点是内置字形，
  不进字库范围。
- spec 正文 v1 定稿不动，落地时并入 v1.x；ADR 0007 为本 issue 的决策记录。

## 落地（2026-10-16）

按上面的「已定契约」全部做完，宿主与真机两侧都实测过。实现细节集中在 ADR 0007 的「落地（2026-10-16，issue 17）」小节，
这里只记验收与代价：

- **交付物**：`include/embark/hal/filesystem.h`（IFileSystem，另加 `size`：记账要按文件字节数计费）、
  `platform/host/host_filesystem.{h,cpp}`（宿主后端，4 槽，根 = `EMBARK_HOST_FS_ROOT` 否则 exe 旁 `embark_host_fs/`）、
  `include/embark/font_ledger.h` + `src/embark/font_ledger.cpp`（内核层记账，零堆）、
  `platform/common/font_service.{h,cpp}`（把 LVGL 的 `'F'` 盘注册到 IFileSystem）、
  `tools/font/audit_glyphs.mjs`（审计脚本）、`.github/workflows/ci.yml` 的 `font-audit` job、
  `config/embark_limits.h` 五个容量常量 + `docs/reference/limits.md` 同步。
- **测试**：`tests/kernel/test_font_ledger.cpp`（复用计数 / 预算 / 表项 / 错误路径不泄漏）、
  `tests/host/test_host_filesystem.cpp`（非法路径、EOF、seek、槽位耗尽）、`tests/host/test_font_service.cpp`
  （端到端：加载真实 `app/aidogui_ui/export/font/ui_font_ui_heiti_14.bin` → 渲染出非黑像素 → 引用与额度归零 →
  多释放 `not_found`；垃圾文件 `corrupt_data` 且不占额度）。`ctest` = 2/2 通过（`embark_tests` + 新目标 `embark_host_tests`）。
- **审计**：清单 `tools/font/embark_zh_14.json` 按实测界面文案扩到 84 个非 ASCII 字符，字库重生成
  （184 字形，15/15 自检通过，压缩位图 7582 B）；`node tools/font/audit_glyphs.mjs` 在 `app/` `src/` `platform/`
  139 个源文件上 exit 0，`--selftest` 4 项 PASS。判决口径：审计收集的字符由 `--print-required` 得出，
  **不要整体替换清单字符集时漏掉旧清单已覆盖的字**（本次踩过一次：77 字里丢了「动启器时置设钟」6 字）。
- **字库真的进了 flash**（本 issue 的「静态子集烧 flash 是主路径」此前是空头支票）：原先没有任何代码引用
  `embark_zh_14`，归档成员被 ld 丢弃。现在 `config/lv_conf.h` 用 `LV_FONT_CUSTOM_DECLARE` +
  `LV_FONT_DEFAULT &embark_zh_14` 接成默认字体，两侧整体收进链接（宿主 `-Wl,--whole-archive`、真机 `WHOLE_ARCHIVE`）。
  真机 map：`embark_zh_14` 29 处命中、`lv_font_montserrat_14` 0 处，固件 784592 B（比接线前还小 2288 B）。
- **未做（按契约推迟）**：真机 SD 后端与 PSRAM 显式分配 → 新开 `.scratch/embark-v1/issues/25-esp32-sd-filesystem.md`；
  FreeType 保持关闭；App 层 v1 不使用运行时字库（界面文案全在静态子集里，运行时加载目前只有宿主端到端用例在用）。

## Comments

- 2026-10-16：落地完成，`Status: open → resolved`。决策记录见 ADR 0007；真机 SD 部分拆到 issue 25。
