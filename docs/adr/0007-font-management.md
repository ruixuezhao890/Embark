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
