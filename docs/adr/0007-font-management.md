# 字体：静态子集烧 flash 为主，SD→PSRAM 运行时加载为备（接口先行）

用户在设计问答中提出（"字体怎么管理，在单片机上不可能说全部烧录到flash吧？可以从sd卡加载到
psram中再进行引用吧？"）。核查仓库与 LVGL 8.3.11 后的决策：

1. **两层字库策略**：
   - **静态子集烧 flash**（主）：启动器、框架壳、元数据标题等全部 UI 文本用 lv_font_conv 生成的
     C 数组压缩位图，只收实际用到的字符（十 KB 级）；SD 不存在/未挂载也能开机完整渲染。
   - **运行时加载**（备，给未来的动态文本：文件名、歌名、用户数据）：LVGL 8.3.11 原生
     `lv_font_load(path)` 与 `lv_font_free()`（`src/font/lv_font_loader.h`）走 LVGL 文件系统读字库，
     宿主后端读可执行文件旁目录，字库文件与 lv_font_conv 数据同源打包；真机侧 = SD 后端 +
     PSRAM 显式记账（8 MB 八线 PSRAM，容量不是问题，失控才是）。
2. **接口先行，SD 后置**：新增框架级 `IFileSystem`（open/read/seek/close），宿主后端现在就能
  落地与测试；真机 SD 后端是后续 issue——前置门槛是 HAL 面（当前 `esp32_bus.h` 明写
  `spi_transfer` 如实返回 `unsupported`，"等真要挂 SD 卡之类的外设时"先加 HAL 面），
  板子 SD 接线需用户手册确认。
3. **字库记账**：运行时字库统一过记账层（字节预算 + 引用计数，超预算返回 `no_space`），
  独立于 LVGL 全局堆预算（256 KB 静态池）之外显式管理；esp32 真机映射为 PSRAM 分配。
4. **FreeType 推迟**：`src/extra/libs/freetype` 内树可用（`LV_USE_FREETYPE` 门控，缓存
   `LV_FREETYPE_CACHE_SIZE` 默认 16 KB），但按需光栅化有 CPU/内存/库体积开销；在真出现
   "任意 Unicode 文本"需求（音乐、文件浏览）之前不上，`config/lv_conf.h` 保持关闭。
5. **缺字防呆 = 构建期审计**：扫描源码 UI 字符串（app/ + 框架壳）对照静态子集覆盖清单，
   CI 门禁，字库外字符直接构建失败；不在运行期才发现豆腐块。

**否决过的方案**：全量 CJK 烧 flash（十几 MB 量级，flash 16 MB 放得下但毫无必要、冷启动拖慢）；
现在就上 FreeType（当前所有文本静态可知，光栅化开销换不来收益）；完全不用运行时加载（堵死
用户明确要的 SD→PSRAM 路线）。

**代价与影响**：新增 IFileSystem 接口与宿主后端、记账层、字库生成与审计脚本（CI 多一个 job）；
`config/lv_conf.h` 的文件系统驱动暂不开（运行时加载落地时才开，宿主后端先行不依赖它——
`lv_font_load` 的文件读取路径在宿主实现里自持）；真机验证内存走 PSRAM 显式记账。

工作项与验收草案见 `.scratch/embark-v1/issues/17-font-service-and-audit.md`。

## 落地（2026-10-16，issue 17）

- **静态子集字库**：`tools/font/gen_font.mjs` 生成 `assets/fonts/embark_zh_14.c`（Noto Sans SC 子集，
  14 px / 4 bpp / 压缩位图；当前 184 字形 = ASCII 0x20–0x7E 的 95 + 界面文案 84 + 图标 5，约 76 KB 源文件）；
  覆盖清单 `tools/font/embark_zh_14.json` 是审计的唯一依据。生成后对 4bpp 位图做 alpha 提亮（gain 2.4），
  抵消 lv_font_conv 在小字号 4bpp 上的过淡；提亮前先跑一次「解码 → 提升 → 编码 → 再解码」回环断言。
- **默认字体接上（2026-10-16 补）**：字库此前**没有任何代码引用**（注释写着「导航壳退役后保留备用」），
  于是 `libembark_font.a` 的成员在链接期被整体丢弃 —— `build-esp32/embark_esp32.map` 里搜不到 `embark_zh_14`，
  唯一的 `.rodata.glyph_bitmap` 是 LVGL 自带的 montserrat_14，即压缩位图根本没进镜像。现在
  `config/lv_conf.h` 用 `#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(embark_zh_14)` +
  `#define LV_FONT_DEFAULT &embark_zh_14` 把它接成 LVGL 的默认字体（声明必须走 `LV_FONT_CUSTOM_DECLARE`：
  lv_conf.h 的包含点 `lv_font_t` 还没定义，直接写 `extern` 不行）。默认字体一换，`liblvgl.a` 里的
  `lv_theme.c.obj` / `lv_draw_label.c.obj` 就会引用它，而 GNU ld 只在「扫到该归档时符号仍未定义」才拉成员、
  库顺序不由我们决定 ⇒ 宿主报 `undefined reference to 'embark_zh_14'`（真机同一错误来自 `lv_draw_label.c.obj`）。
  两侧都用**整体收进链接**解决：宿主 `-Wl,--whole-archive embark_zh_font -Wl,--no-whole-archive`，
  真机 `idf_component_register(... WHOLE_ARCHIVE)`（试过 `-u;embark_zh_14`，宿主上 CMake 把裸名字变成
  `-lembark_zh_14`，无效）。结果：真机 map 里 `embark_zh_14` 命中 29 处、`lv_font_montserrat_14` 命中 0 处，
  固件反而**小了 2288 B**（784592 B）—— 丢掉了自带的 14 号。这就是「静态子集是主路径」的运行期承载者：
  任何没显式设字体的文本都走它，缺字审计保证的字真的会被画出来。
- **IFileSystem**：`include/embark/hal/filesystem.h`——只读字节流 `open/read/seek/close/size`（另加 `size`：
  记账要按文件字节数计费），句柄 `FileHandle = uint16_t`，全 `noexcept` + `Error` 码。宿主后端
  `platform/host/host_filesystem.{h,cpp}`（根 = 环境变量 `EMBARK_HOST_FS_ROOT`，否则可执行文件旁
  `embark_host_fs/`；定长 4 槽；拒绝绝对路径 / `..` / 含 `:` 的路径）。`hal::Context` 里它是**可空指针**
  `IFileSystem* filesystem`——真机还没有盘，那里是 `nullptr`。
- **字库记账**：`include/embark/font_ledger.h` + `src/embark/font_ledger.cpp`——内核层，零堆（定长表）、
  不依赖 LVGL，只做「字节预算 + 引用计数」；容量见 `config/embark_limits.h`。
- **字库服务**：`platform/common/font_service.{h,cpp}`——把 LVGL 的 `'F'` 盘注册到 `hal::IFileSystem`
  （回调与驱动描述是文件级静态量，一个进程只能有一个活动实例）；`load()` 先记账再 `lv_font_load`，
  失败退还额度；同路径复用只 +1 引用，引用归零才 `lv_font_free`。不依赖 `lv_conf.h` 的 FS 驱动开关。
- **缺字审计**：`tools/font/audit_glyphs.mjs`（Node，零依赖）扫描 `app/`、`src/`、`platform/` 里所有会把
  字符串送上屏的调用（`lv_label_set_text` 族与 checkbox/textarea/dropdown/roller/btnmatrix/table/
  tabview/list/msgbox 的 setter），逐字符对照清单；缺字打印字符 + 文件:行号并 exit 1。LVGL 图标区
  `U+F000–U+F8FF` 默认豁免（内置字形，不进清单），`--include-symbols` 可关掉豁免。另有 `--print-required`
  （打印该进清单的字符集）与 `--selftest`（自造缺字夹具验证报错路径）。
- **CI**：新增 `font-audit` job（先 `--selftest` 再审计），见 `.github/workflows/ci.yml`。

**本次未做（按本 ADR 的决策推迟）**：真机 SD 后端与 PSRAM 显式分配（要先加 HAL 的 SPI 面 + 板子接线确认，
见 `.scratch/embark-v1/issues/25-esp32-sd-filesystem.md`）；FreeType（`config/lv_conf.h` 保持关闭）；App 层 v1
不使用运行时字库——界面文案全在静态子集里。

**测试**：`tests/kernel/test_font_ledger.cpp`（记账纯逻辑：复用计数 / 预算 / 表项 / 错误路径不泄漏）、
`tests/host/test_host_filesystem.cpp`（宿主后端：非法路径、EOF、seek、槽位耗尽）、
`tests/host/test_font_service.cpp`（端到端：加载真实 `ui_font_ui_heiti_14.bin` → 渲染出像素 → 引用与额度归零
→ 多释放报 `not_found`；不是字库的文件报 `corrupt_data` 且不占额度）。
