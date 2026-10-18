# 常见坑（v1 期不断追加）

按踩坑频率排序。多数条目在代码注释里也有出处，这里给"为什么会这样"。

## ETL 定容：容量是编译期的，行为是"满了怎么办"写死的

- 容器只用 ETL 固定容量版，容量全在 `config/embark_limits.h`（单一事实来源）：
  `max_apps` / `max_bus_subscribers` / `max_background_timers` /
  `message_queue_depth` 等。改容量 = 改那里，不要散落在各处。
- 满了的行为各容器不同、且**不都报错**：
  - `etl::circular_buffer::push`：满时**覆盖最旧**（收件箱就是这样：溢出计数
    + WARN，不丢新消息）；
  - `etl::message_bus`：订阅上限是编译期容量，自己维护的镜像表查重后
    超限返回失败（framework 装配时处理为 `Error::no_space`）；
  - `etl::vector` 之类在容量外是 assert/未定义，别依赖扩容。
- 所有这类行为在写代码前查对应 ETL 头文件（`third_party/etl/include/etl/`），
  README 级别文档可能滞后——20.49.0 的若干反直觉点见
  `.scratch/embark-v1/spec.md` §16.6。

## 消息类不是聚合：必须写显式构造函数

```cpp
struct BrightnessMessage : public embark::MessageT<0x21> {  // 有基类！
  constexpr BrightnessMessage(std::uint8_t v = 0) noexcept : level(v) {}
  std::uint8_t level = 0;
};
```

有基类（`etl::imessage` 虚表）的类不是聚合，`BrightnessMessage{5}` 的
大括号初始化会编不过。demo 的写法就是标准答案。

## 消息 id 别撞保留区

`0xFE` = `cross_task_message_id`（跨任务信封，框架自留）。自己的消息 id
避开它，并在定义处注释分配理由（demo 用 0x21）。

## kernel 里没有异常，也没有堆

- 内核与 App 默认 `-fno-exceptions -fno-rtti`、零堆分配：`throw` 编不过，
  `new` 会编不过或必须走静态池。失败用 `embark::Error` /
  `etl::expected`（`unexpected(...)` 便捷函数）。
- `tests` 目标没有这些限制（才能装全局 new 计数钩子做零分配审计，
  见 `tests/detail/zero_alloc_hooks.cpp`）；那是测试专用，不要照抄进内核。
- 宿主 LVGL 的堆由 `platform/host/host_lvgl_mem.cpp` 显式配置；
  真机侧同样要显式给（静态池或 NVS 外的专用区）。

## MinGW 没有 std::aligned_alloc

`<cstdlib>` 的 `std::aligned_alloc` 在 MinGW libstdc++ **不存在**（编译错误，
不是运行错）。对齐分配用 CRT 的 `_aligned_malloc` / `_aligned_free`
（`tests/detail/zero_alloc_hooks.cpp` 是例子）。

## 宿主 Context 的成员是引用，不是函数

`hal::Context::time` 等是**引用成员**（`ITime& time;`），不是返回引用的
函数。写 `hal.time.now_ms()`，别写 `hal.time().now_ms()`（编不过的就是它）。

## efmt 格式化参数上限 16

`ELOG_INFO` 一行展开的格式参数最多 16 个（efmt `max_format_args`）。
统计行超过就拆两条日志（ui_demo.cpp 的统计输出就是这么拆的）。
日志钩子请用 `ELOG_*` 门面（整行串行化），不要裸 printf。

## 日志行上限 384 字节：超了整行消失，不是截断

`ELOG_*` 的单条日志 = 时间戳/位置前缀 + 正文，放不下 `ELOG_MAX_RECORD_SIZE`
（`third_party/efmt-elog/elog/elog.hpp:64`，默认 **384** 字节）就**整行丢弃**：
`elog.hpp:186` 的注释与 `:211-220` 的实现都是"不输出半行"，不报错、不截断。
派生打印会显著拉长日志行（一个结构体就能上百字节），所以：

- 一条日志只放一个整对象；多个对象拆两条 —— 宿主演示的收尾统计就是这么拆的
  （`platform/host/ui_demo.cpp:319-330`，"显示与前台"一条、"启动器"一条、"后台/消息"一条）。
  拆完发现少了半行、或者某条日志凭空不见，先量长度。
- 这块缓冲是 `log_at` 里的局部数组 `char record[384 + 1]`（`elog.hpp:203`）：
  每打一行，调用者栈上就占 385 字节。但它只是**零头**，大头是下面这条（issue 22 实测）。
- **efmt 的 `std::ostringstream` 兜底一次吃掉 ~1.9 KB 栈**（issue 22 实测：1936 字节）：
  efmt 对没有原生格式化器的类型走流式兜底（`efmt/core/format_traits.hpp:343`；
  `EFMT_ENABLE_HOSTED` 在 ESP-IDF 上默认 1 ⇒ `EFMT_ENABLE_STREAM_FALLBACK = 1`）。
  现场：`own_task` 请求 256 字（= 2048 字节）的栈，一条打印 `AppId`（`std::uint16_t`，
  2 字节整型**没有**原生格式化器）的 `ELOG_INFO` 就踩穿栈缓冲底、把自己的 TCB 写坏 ⇒
  core 0 在 SysTick 的 `xTaskIncrementTick` 里 `LoadProhibited`（回溯与寄存器现场见
  issue 22）。所以在 `own_task` 或小栈任务里打日志要按这几条办：
  - 会进日志的字段**别用 `std::uint16_t`**（以及任何无原生格式化器的类型）；
    优先 `std::uint32_t` / `std::uint64_t` / `int`。`AppId`、`AppSettings` 的成员
    是被 issue 13 的 1 字节问题坑成 2 字节的，2 字节正好落在流式兜底上 —— 两头都要记。
  - 要打日志就按 **≥ 3 KB 栈**准备（`task_stack_words >= 384`）：1.9 KB 给这条日志 +
    调用者自己的帧；4 KB（512 字）是留出余量的低线。
  - 想彻底堵住：真机关掉 `EFMT_ENABLE_HOSTED`，没有原生格式化器就编译报错，
    运行期不会再有这条路径（取舍见 issue 22）。
- 栈预算的单位：框架的 `*_stack_words` 一律按**宿主字长**算（x86-64 1 字 = 8 字节，
  512 字 = 4 KB），真机在 `platform/esp32/src/esp32_board.h` 里用
  `stack_word_bytes = 8` 换算成字节再交给 IDF —— 因为 IDF 的 `ulStackDepth` 是**字节**，
  而 xtensa 的 `StackType_t` 是 `uint8_t`，直接照搬字数只剩 1/8（issue 21 就是这么把
  16 KB 的 UI 任务变成 2 KB 的）。所以同一个常量在宿主和真机上都是同样的字节数。
- 想调大就改 `ELOG_MAX_RECORD_SIZE`（`config/embark_config.h` 里登记了这件事），
  但栈成本同步上涨。

## 派生打印（E_FMT_DERIVE）：类型的名字只有一份

v1 的打印约定是"类型自己声明怎么打"，不再手写 `to_string`（issue 13）：

- 枚举用 `E_FMT_DERIVE_ENUM(enum class Error : std::uint8_t { ... });`、
  结构体用 `E_FMT_DERIVE(struct Rect { ... });`；调用处补分号，
  **声明里一行只写一个字段**（宏按逗号切宏参数）。类里有基类/构造函数、
  声明里带 `#if` 或字段超过 16 个时，改成在类型体内写
  `E_FMT_FIELDS(from_app, seq);`（`include/embark/message.h` 的
  `CrossTaskMessage` 是例子）。
- 用法就是整对象填空：`ELOG_INFO("HAL 就绪：显示 {}", display_info);`，
  输出是带类型的 Rust Debug 风格：`embark::hal::Rect { x = 0, y = 0, width = 240, height = 320 }`。
- **1 字节整型成员会被当成字符打**（上游 efmt 的已知问题，已登记）：派生输出里
  `std::uint8_t` / `std::int8_t` / `unsigned char` 成员走字符通道，值为 0 时写出
  NUL，会把整行日志**截断**。所以框架里会进日志的字段一律 2 字节起：`AppId`、
  `AppSettings::task_priority`、`InputEvent::key` 都是 `std::uint16_t`
  （原因写在 `include/embark/message.h` 的 `AppId` 处）。顶层 `std::uint8_t`
  参数打印是正常的（`key = 113` 就来自这里），别把这条推广过头。
- 只吃 `const char*` 的出口（`fprintf`、`embark::fatal`）用
  `char text[24]; embark::error_text(text, error);`。
- 没登记的类型在 `EFMT_DERIVE_STRICT`（默认 1）下是**编译报错**，不会静默降级；
  想省 Flash 可以 `-DEFMT_DERIVE_SHOW_TYPE=0` 去掉类型名（输出变成
  `Error::not_found`，代价是不同类型之间会撞名）。派生是纯编译期注册，
  运行期零开销、零堆；成本只有 Flash（结构体约 1 KB/类型、枚举约 0.3 KB/类型）。

## ELOG_WARN 在测试里是 no-op（不是 bug)

没有注册默认 logger 时 `ELOG_WARN` 什么都不做（`log_at` 无后端）。
内核在测试里因此"静默"是预期行为；宿主里接了 `host_log_sink` 就正常打。

## 宏前置条件（config/embark_config.h 是唯一开关）

- `EMBARK_PLATFORM_HAS_FREERTOS`：顶层默认 ON；真机与宿主都靠它。
- `ETL_TARGET_OS_FREERTOS`：ETL 据此选 `mutex_freertos.h` 等（需要 FreeRTOS
  queue 符号，链路已通过 `embark_freertos` 静态库满足）。
- `ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK`：callback_timer 的锁二选一宏
  （ATOMIC / INTERRUPT），**已定义 ATOMIC**；不要两个都开，也不要都关。
- `lv_conf.h` 在 `config/` 是单一事实来源，LVGL 构建视图从那里取；
  改 `LV_COLOR_DEPTH`（16 = RGB565，spec 定的）或开关要在那一份上改。

## ESP32-S3 真机（IDF）特有

- **配置文件必须纯 ASCII**：`platform/esp32/project/sdkconfig.defaults` 与
  `partitions.csv` 由 IDF 的 `kconfgen` / `gen_esp32part.py` 按**宿主本地编码**
  读取（中文 Windows = GBK）。UTF-8 中文注释会让 `set-target` 直接失败：
  `UnicodeDecodeError: 'gbk' codec can't decode byte ...`。中文说明写在
  `platform/esp32/README.md`，那两个文件里只留英文注释。
- **`CONFIG_FREERTOS_HZ` 必须 ≥ 1000**：UI 循环是 5 ms，100 Hz 下
  `pdMS_TO_TICKS(5) == 0` ⇒ `vTaskDelay(0)` 只让出一次调度 ⇒ UI 任务忙等、
  拖住空闲任务、触发任务看门狗。后端另有"0 tick 退化成 1 tick"兜底，
  但底子还是 1 kHz。
- **栈深的单位**：IDF 的 `xTaskCreate*` 收**字节**（`xTaskCreateStaticPinnedToCore`
  也一样），`uxTaskGetStackHighWaterMark()` 返回**字**（xtensa 上 1 字 = 1 字节，
  所以数值上等于字节）。框架的 `*_stack_words` 按宿主字长（8 字节）算，唯一的换算点是
  `platform/esp32/src/esp32_board.h` 的 `stack_word_bytes`：UI 任务走
  `ui_task_stack_bytes`（`esp32_ui_task.cpp` 直接收字节），`own_task` 的栈数组
  `Esp32Task::stack[own_task_stack_bytes]` 与 `xTaskCreate` 的深度都从那里取。
  别在别处再乘一次 `sizeof(StackType_t)` —— issue 21 的原始 bug 就是它等于乘 1。
- **`uint32_t` 不等于 `unsigned int`**：xtensa GCC 下 `uint32_t` =
  `long unsigned int`，所以 `lv_label_set_text_fmt(label, "%u", ticks)` 这类
  **printf 风格**调用会被 IDF 的 `-Werror=format` 拦下（宿主 MinGW 上两者同类型，
  永远不报）。写 `%` 格式时显式 `static_cast<unsigned>(...)`；框架自己的日志是
  efmt 的 `{}` 风格，不受影响。
- **引脚常量要显式转 `gpio_num_t`**：`esp32_board.h` 故意不引 IDF 头，常量是 `int`；
  IDF 的 `i2c_master_bus_config_t::sda_io_num` 之类字段是枚举 ⇒ 必须
  `static_cast<gpio_num_t>(...)`，少了就是
  `invalid conversion from 'int' to 'gpio_num_t'`。
- **中间件视图要在 `project()` 之前建**：IDF 的组件配置发生在 `project()` 里，
  `<middleware/...>` 的 junction 必须先存在；而且 `embark_create_middleware_view`
  要显式传仓库根（IDF 工程的 `PROJECT_SOURCE_DIR` 是 `platform/esp32/project`，
  不是仓库根）。
- **`esp_lcd` 的像素发送是异步的**：`tx_color` / `draw_bitmap` 只是入队，缓冲区要等
  `on_color_trans_done` 才能复用；而且**一次调用只回调一次**（驱动只在最后一块
  chunk 上置 `en_trans_done_cb`）⇒ 后端的 `flush()` 用静态二值信号量等它，
  等到了才算 HAL 要的"同步语义"成立。
- **组件要自己补宿主 CMake 给过的东西**：`-include config/embark_config.h`、
  `third_party/etl/include`（ETL 是 header-only，宿主靠 `etl::etl` 的 INTERFACE
  目录）、`EMBARK_PLATFORM_NAME` / `EMBARK_VERSION_STRING`。
- **源清单只有一份：`cmake/embark_sources.cmake`（issue 20 的结构性收口）**：宿主与真机
  都 `include` 它取 SRCS / INCLUDE_DIRS，加/删 TU 只改这一处。以前是四份手抄名单
  （`src/CMakeLists.txt`、`app/CMakeLists.txt`、`platform/common/CMakeLists.txt`、真机组件的
  `EMBARK_KERNEL_SOURCES` 等），一份源码树、多条构建路径（spec §11）：删
  `src/embark/error.cpp` 时只改了宿主那份 ⇒ 宿主全绿、真机 configure 期直接失败
  `Cannot find source file: .../src/embark/error.cpp`（显式列出的源文件不存在时 CMake
  不跳过，直接报错）；2026-10-16 又漏了真机三个 App 的 include 目录 ⇒ 编译期
  `clock_app.h: No such file or directory`。清单自带**配置期审计**（每次 cmake 配置跑一次）：
  登记的路径不存在、归口目录里有 `.cpp` 没登记、真机 App 子集不在全量里、真机 main.cpp
  的裸头名找不到 include 目录 —— 四条都在配置期 `FATAL_ERROR`，不再拖到真机链接期。
  **新加 App 不必手工登记**：`node tools/new_app.mjs <名> --apply` 一次改完 5 行 / 3 个文件
  （两份源清单 + `platform/host/ui_demo.cpp` 的 include 与 App 表 + `aidogui_ui_hooks.h`）。
  手工加就得自己补齐这 5 处：漏一份清单的表现是「宿主能跑、真机编不到」或反过来；
  漏 include 是编译期 `'XxxApp' is not a member of 'embark::demo'`。
  **加普通 `.cpp`（不是 App）仍必须手工登记**到 `EMBARK_APP_SOURCES` 或对应的源清单。
- **组件的 `PUBLIC` 编译选项会漏到 IDF 生成的纯 C 文件上**：`PUBLIC` 选项会跟着
  组件传播给最终链接目标 `embark_esp32.elf`，而那个目标要编一个自动生成的
  `project_elf_src_esp32s3.c`（**纯 C**）。于是 `-include config/embark_config.h`
  这种 **C++ 专用**的选项一旦写成 `PUBLIC`，C 文件看见 ETL 头就是一片
  `error: unknown type name 'namespace'`。规矩：`-include` 留在组件 `PRIVATE`，
  依赖方（`project/main/CMakeLists.txt`）自己带一份；`EMBARK_*` 宏定义可以 `PUBLIC`
  （纯宏，C 也吃得下）。
- **真机组件不带 `-Wpedantic`**：IDF/LVGL 的头文件在 `-Wpedantic` 下会刷
  `#include_next is a GCC extension`、匿名结构体、柔性数组等警告（宿主上这些头是
  SYSTEM include，IDF 侧没有这层区分）。我们自己的代码在宿主侧仍受 `-Wpedantic` 管。

## CMake：C 语言工具链变量不外泄

- 子目录 `project()` 隐式启用 C 语言时，`CMAKE_C_COMPILE_OBJECT` /
  `CMAKE_C_ARCHIVE_CREATE/FINISH` 等工具链规则变量只在**该子目录作用域**装载
  （由 `Modules/CMakeCInformation.cmake` 装载，从不写进 `CMakeCCompiler.cmake`）。
  兄弟作用域建纯 C 库会生成期报「Missing variable is: CMAKE_C_COMPILE_OBJECT」。
- 修法：根 `project()` 显式 `LANGUAGES C CXX`（本仓库已做，CMakeLists.txt 顶部
  有注释）；不要在子目录里挪 `C_STANDARD` / `target_compile_features` 试图"修"它
  ——那是红鲱鱼。
- `EMBARK_PLATFORM_HAS_FREERTOS` 这类 `option()` 不会自愈已有 cache 条目：
  改默认值后要删 cache 或显式 `-D...=ON` 重配。
## `--screenshot` 写不进仓库外的绝对路径（实测，2026-10-18）

- **用相对路径**：`./embark_host_ui.exe --frames 60 --screenshot probe.bmp` 落到当前目录，稳。
- 写仓库外的绝对路径（`E:/…`、`C:/Users/…/Temp/…`）在本机实测会失败，报
  `截图失败：写不进 <路径>（Couldn't open <路径>）` —— 而同一个目录用 shell / node 写是好的，
  所以别怀疑权限；大概率是这台机器的沙箱/杀软对宿主进程的转手限制（未定论）。
- 无论哪种失败，日志都会带上完整路径（`platform/host/ui_demo.cpp` 的 `save_screenshot`），
  照路径改就行 —— 这一条就是为了不再出现「`Couldn't open` 但不告诉你是哪个路径」。

## 宿主特有

- SDL2 找不到：`embark_host_ui` 目标被跳过（STATUS 提示），内核与测试照常
  构建——不是错误。CI 里 apt 装 `libsdl2-dev` 后它会出现。
- 持久化文件：默认写 exe 同目录 `embark_host_kv.bin`；删掉或设
  `EMBARK_HOST_STORAGE=<路径>` 换位置（自检每次 `boot_count` +1）。
- PowerShell 下别用管道给 `git commit` 传消息（有假失败先例）；直接
  `git commit -m "..."`。别用 PowerShell 重写源码文件（ANSI 编码会毁中文
  注释），要用编辑工具。

## 测试特有

- doctest 断言失败在 Windows 表现为 DebugBreak（ctest 报 SegFault），
  不是真段错误；用 gdb/直接跑 exe 区分。
- 静态 App 注册表实例在测试间共享：用例之间显式 reset/rebind 观测对象
  （test_framework_messaging.cpp 有先例），避免悬垂访问。

## LVGL 界面文案

界面字体是**卡上那份 TTF**（`app/aidogui_ui/assets/font/ui_heiti.ttf`，运行期用 `lv_tiny_ttf`
栅格化，ADR 0017）；内置的 `embark_zh_14`（184 字形）只剩「没插卡」这一个角色，汉字会成方框。
**新增界面文案后跑 `node tools/font/audit_glyphs.mjs`**（CI 也跑）：它对照 TTF 的 cmap，缺字会
exit 1 并给出行号。缺字的出路是换字体 / 改文案 / 给该 label 显式指定含这个字的字体 —— **不是**
再补一份子集清单（BIN 那条链已经删了）。框架与日志的中文不受影响（那是宿主侧输出）。

## ESP-IDF 组件 CMakeLists 会被执行两遍（issue 25 踩到）

`platform/esp32/project/components/*/CMakeLists.txt` 里的 `include(...)` **会在两种模式下各跑一遍**，
第二遍是**脚本模式**（`cmake -P tools/cmake/scripts/component_get_requirements.cmake` —— IDF 收组件
REQUIRES 时用它跑一遍各组件 CMakeLists）：

- 脚本模式里没有配置阶段，`file(GLOB ... CONFIGURE_DEPENDS)` 直接报
  `CONFIGURE_DEPENDS is invalid for script and find package modes`，整个配置断在这里。
  所以 `cmake/embark_sources.cmake` 的审计段前面有一句 `if(CMAKE_SCRIPT_MODE_FILE) return() endif()`
  （变量定义要照常执行 —— 脚本模式还得能解析 `idf_component_register`）。
  **症状隐蔽**：issue 20 收口后一路正常，直到有人改了 REQUIRES（IDF 才重跑那个脚本）才炸。
- `idf_component_register(... REQUIRES ...)` 的参数**写一行**：跨行或夹注释会被抄进生成的
  `build_properties.temp.cmake`，报
  `Syntax Warning … Argument not separated from preceding token by whitespace`，并被 IDF 当致命错误打出来。

## 音频 / I²S（issue 09 踩到）

- **xtensa 上 `std::atomic_flag` 链接失败**：报 `undefined reference to '__atomic_test_and_set'`，
  因为 xtensa 的默认库里没有那个 libatomic 符号（宿主的 MinGW 有，所以只在真机构建暴露）。
  真机侧改用 FreeRTOS 互斥量（`xSemaphoreCreateMutex` / `xSemaphoreTake(lock, portMAX_DELAY)`），
  不要把宿主的 `atomic_flag` 写法直接搬过去。
- **别拿固定 `sleep` 当音频同步点**：宿主 SDL 的音频回调很稀疏（实测 300 ms 里只来 1 次 21 ms），
  首版验收写「sleep 300 ms 后断言位置前进」直接失败了 —— 那是在测机器心情，不是在测状态机；
  真机 I²S 更没这个保证。改成轮询 `wait_until_advanced()`（deadline 3 s、步长 10 ms）。
- **`sizeof` 带命名空间的类型名要当心**：`sizeof std::int16_t` 在 GCC 15（MinGW）报
  `error: expected primary-expression before ',' token`（被当成表达式解析）。写 `sizeof(buf[0])`
  或 `sizeof(std::int16_t)` 加括号。
