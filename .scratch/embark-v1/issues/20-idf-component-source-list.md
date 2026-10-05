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

## 备注

- 现在仍是两份手抄清单 + 一条注释提醒。结构性收口（把宿主/真机两份列表抽成单一 CMake 变量或生成式
  清单，让"漏项"变成 CMake 配置期错误）没有做 —— 记在这里，等下一次真的被咬再动。
- 同一批 bring-up 缺陷还有 issue 21（任务栈口径）、issue 22（own task 里的 ELOG 栈预算）。
