# 20 · 真机组件源列表漏项：编译期看不出来，链接期 undefined reference

Status: resolved
Type: task
Blocked by: 16, 17
来源: 真机 bring-up（首次烧录 —— issue 11「换后端不动 App」里"能 build 到能跑"之间掉出来的洞）

## 现象

真机侧的两份清单是**手抄**的，宿主侧改了、真机侧没跟上，代价全部落在链接期：

- platform/esp32/project/components/embark/CMakeLists.txt 的 EMBARK_COMMON_SOURCES 与宿主
  platform/common/CMakeLists.txt 的源列表各自维护。issue 16 新增 platform/common/nav_shell.cpp
  （导航壳：状态行 + 返回键）时只改了宿主那份，而 platform/common/lvgl_ui_port.cpp 引用
  embark::nav_shell::init —— 编译期只看到声明，一切正常，到 Linking CXX executable embark_esp32.elf
  才报 undefined reference。
- 字库 assets/fonts/embark_zh_14.c（issue 17 的 lv_font_conv 生成物）在真机侧根本没有对应的编译单元：
  lv_font_embark_zh_14 之类的符号无人定义。

为什么这两类洞宿主 CI 抓不到：宿主链接走的是 platform/common 那份**正确**的列表，三个 CI job 全绿；
只有真机链接会把"清单不一致"变成错误。这正是 issue 11 的代价面 —— 后端要自己维护一份源列表，
却没有机制保证它与宿主一致。

## 修复

- platform/esp32/project/components/embark/CMakeLists.txt:25-32：EMBARK_COMMON_SOURCES 补
  ${EMBARK_ROOT}/platform/common/nav_shell.cpp，并在注释里写明「漏在这里的后果不是编译错误而是
  **链接期** undefined reference，见 issue 20」。
- 新增 IDF 组件 platform/esp32/project/components/embark_font/（CMakeLists.txt:16-19）：
      idf_component_register(
        SRCS "${EMBARK_ROOT}/assets/fonts/embark_zh_14.c"
        INCLUDE_DIRS "${EMBARK_ROOT}/config"
        REQUIRES lvgl)
  并在 embark 组件的 REQUIRES 里加 embark_font
  （REQUIRES lvgl nvs_flash esp_timer esp_lcd driver → REQUIRES lvgl embark_font nvs_flash esp_timer esp_lcd driver）。
- 为什么单独开组件、而不是把 .c 塞进 embark 组件（该文件 CMakeLists.txt:3-10 有同样的注释）：
  embark 组件对**所有**源文件强制 -include config/embark_config.h（ETL 的编译期宏入口，C++-only），
  套在 lv_font_conv 生成的纯 C 上就是一片 unknown type name 'namespace'。宿主侧同样是单独一个库
  （platform/common/CMakeLists.txt 的 embark_zh_font），真机保持同一形状。

## 验收

- idf.py -C platform/esp32/project -B build-esp32 build → BUILD-EXIT=0，
  embark_esp32.bin binary size 0xc14a0 bytes … 0x23eb60 bytes (75%) free
  （见 evidence/20-source-list/build-after.txt）。
- 链接证据（不是"编译过了"）：真机启动日志出现
  [nav_shell.cpp:99 init] 导航壳就绪：状态行高 28，返回键中心 (15, 14)，时间源 1 s 定时器
  —— 说明 nav_shell.cpp 既被链接又被执行；中文字库由首屏绘制间接验证（issue 19 的 EEZ 界面）。
- 复现方式（留档）：删掉上面两处再 build，在 Linking CXX executable embark_esp32.elf 处报 undefined reference。

## 结构性收口（2026-10-16 补）

上面那句「等下一次真的被咬再动」在 2026-10-16 被咬了第二次：App 目录化（2026-10-06 起各占
`app/<App 名>/`）之后，真机组件的 INCLUDE_DIRS 没跟上，`main.cpp:20-22` 的
`#include "clock_app.h"` 直接 `fatal error: clock_app.h: No such file or directory`。
于是把备注里那条补上：**漏项现在是配置期错误，不再是链接期/编译期事故**。

- **单一事实来源 = `cmake/embark_sources.cmake`**：宿主（顶层 `CMakeLists.txt` 的
  `include(embark_sources)`，供 `src/`、`platform/common/`、`app/` 三处取 SRCS）与真机
  （`platform/esp32/project/components/{embark,embark_font}/CMakeLists.txt` 各自 include）
  读同一份。变量划分＝收口 A（内核 `EMBARK_KERNEL_SOURCES`）、B（共用胶水
  `EMBARK_COMMON_*`）、C（App include 目录 `EMBARK_APP_INCLUDE_DIRS_DEVICE`，从源文件所在
  目录推导 —— 2026-10-16 漏的正是这条）、D（字库 `EMBARK_FONT_SOURCES`）。真机 App 子集
  `EMBARK_APP_SOURCES_DEVICE` ⊆ 宿主全量 `EMBARK_APP_SOURCES`；真机专用目录用**减法**得到
  （`EMBARK_COMMON_SOURCES_DEVICE` = 定容池 + LVGL/UI 端口），宿主专属（`font_service.cpp`、
  `aidogui_res_fs.cpp`）显式声明成 host-only —— 归口目录里任何没写进清单的 `.cpp` 都会踩下面审计。
- **配置期审计（四条，全在 `FATAL_ERROR`）**：①登记的路径不存在（改名/删除没同步）；
  ②归口目录（`src/embark`、`platform/common`、`app/*/`、`platform/esp32/src`、`assets/fonts`）里
  有源文件没登记 —— 用 `file(GLOB ... CONFIGURE_DEPENDS)`，新增/删除文件会在下一次构建自动
  触发重配，当场就断，不必等谁手工重新 cmake；③真机 App 子集不在宿主全量里；
  ④真机 `main.cpp` 里 `#include "xxx_app.h"` 的头在某个 App 目录里存在、但推导出的 include
  目录里没有（这条正是 2026-10-16 那次事故的形状：新 App 注册进真机 APP_TABLE 却忘了进子集）。
  审计用 GLOBAL 属性保证一次配置只跑一遍（清单被多个组件 include）。
- **负面验证（实测）**：往 `app/` 放一个没登记的 `.cpp` → `cmake -S . -B build` 报「发现没登记的
  源文件」exit 1；把清单里 `app/album/album_app.cpp` 改成一个不存在的名字 → 报「EMBARK_APP_SOURCES
  里登记的源文件不存在」exit 1；恢复后 configure exit 0。
- **回归（实测，2026-10-16 晚）**：宿主 `cmake --build build` exit 0、`ctest` 2/2 通过、四条验收
  （`--ui` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages`）全过；真机 `idf.py … build` exit 0、
  `embark_esp32.bin` = **0xbf8d0 字节 = 784592 字节**（与收口前同一源集：map 里 `embark_zh_14` 命中
  29 处、`lv_font_montserrat_14` 0 处，app 分区富余 75%）。
- **工具链与文档同步**：`tools/scaffold_app.py` 改成往 `cmake/embark_sources.cmake` 的
  `EMBARK_APP_SOURCES` 插行（锚点＝列表尾的注释 `# 新 App 登记锚点` 之前），
  `tools/scaffold_app_test.py` 的 fake repo 同步（自测仍全过）；
  `docs/reference/pitfalls.md`、`docs/guides/{new-app-guide,quickstart,scaffold-and-tools}.md`、
  `platform/esp32/README.md` 里「两份清单/加文件务必同步那一份」的口径一并改掉。

## 备注

- ~~现在仍是两份手抄清单 + 一条注释提醒；结构性收口没有做。~~ → **2026-10-16 已做**（见上一节）。
- 同一批 bring-up 缺陷还有 issue 21（任务栈口径）、issue 22（own task 里的 ELOG 栈预算）。
