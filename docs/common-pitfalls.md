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
  每打一行，调用者栈上就占 385 字节。`own_task` 的栈只有 `own_task_stack_words`
  （默认 512 字，`config/embark_limits.h`），两边要一起算 —— 注意"字"是 `StackType_t` 字：
  宿主 1 字 = 8 字节（512 字 = 4 KB），真机 xtensa 1 字 = 1 字节（512 字 = 512 字节），
  同一个常量在两种口径下差 8 倍。
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
  也一样），`uxTaskGetStackHighWaterMark()` 返回**字**。框架的 `*_stack_words`
  是字，两处换算分别在 `platform/esp32/src/esp32_ui_task.cpp`（创建与水位）与
  `esp32_system.cpp`（水位）里做掉了。
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
- **内核源列表有两份，加/删 TU 必须一起改**：宿主是 `src/CMakeLists.txt`，真机是
  `platform/esp32/project/components/embark/CMakeLists.txt` 的 `EMBARK_KERNEL_SOURCES`
  （一份源码树、两条构建路径，spec §11）。删 `src/embark/error.cpp` 时只改了宿主那份，
  宿主照样全绿、`cmake --build build-esp32` 却在 configure 阶段直接失败：
  `Cannot find source file: .../src/embark/error.cpp` —— 显式列出的源文件不存在时
  CMake 不会跳过，而是报错。改完记得两边都跑一遍构建。
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

LVGL 内置字体只有 Montserrat（无中文字形），v1 界面文案用英文；中文显示
需要另配 CJK 字体（未纳入 v1）。框架与日志的中文不受影响（那是宿主侧输出）。
## EEZ Studio 生成代码（issue 19）

- 生成代码的 UI 文本进 `app/eez_ui/screens.c`（`lv_label_set_text` 调用），不叫
  "页面资源"；尺寸写死在生成代码里（样例 800×480，本仓库 240×320 已改写），
  换屏分辨率要同步改。
- Flow 的 **User Action / 变量** 由 `actions.cpp` / `vars.cpp` 落地（C++），是
  应用与生成代码之间唯一的桥；`vars.cpp` 的变量是全局 `int32_t`（非 extern "C"），
  读值从 `app/eez_ui_bridge.cpp` 走，别从生成代码头文件绕。
- 生成代码 6 个 .c 源以 `-w` 编译（上游无害警告不归零）；C++ 侧也**必须** `-w`
  （actions.cpp 的 unused parameter 等），否则违反宿主零警告门禁。
- 生成代码以 `EEZ_FOR_LVGL` 分支编译；CMake 给 eez_ui 补 `SYSTEM` include：
  third_party 根（生成代码写死 `<lvgl/lvgl.h>`）与 eez-framework 的
  src/libs/agg/platform/simulator。
- 需要 C 语言的注意：顶层 `project()` 已显式 `LANGUAGES C CXX`（见
  common-pitfalls 的 CMake 小节），子目录再 `project()` 隐式启用 C 的工具链
  规则变量不外泄。
