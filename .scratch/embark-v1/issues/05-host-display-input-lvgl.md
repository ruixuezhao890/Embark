# 05 · 宿主显示/输入后端 + LVGL 集成

Status: resolved
Type: task
Blocked by: 03

## 目标

宿主跑出真实窗口：SDL2 显示/输入后端 + LVGL 8.3.x 集成（`lv_conf.h` 自持）。

## 范围

- SDL2 窗口/纹理 → 显示 HAL（区域刷新、背光映射为窗口亮度/空操作）；SDL2 事件 → 输入 HAL（按键 + 触摸坐标）。
- LVGL 8.3.x：disp/input driver 接线；`config/lv_conf.h` 以 `lvgl_template_laste` 的宿主配置为起点改写（关掉不需要的 widget 与字体，内存相关项显式给定）。
- LVGL allocator 按 spec §10 显式配置（宿主走堆 + 对象总量上限）。
- 先跑通一个最小界面（空白屏 + 一个可点按钮 + 一行文字）证明通路。

## 验收

- 宿主窗口可见、点击有响应；关窗能干净退出。
- LVGL 在这一阶段只被 main 循环调用；issue 06 接手后改由唯一 UI 任务调用。
- 记录 LVGL 补丁版号（spec §15 待定项）到 `config/`。

## 备注

LVGL 用 submodule 引入并固定一个小补丁版；`lv_conf.h` 自持、不依赖上游示例。

## Answer

结论：**已完成**。宿主 SDL2 显示/输入后端 + LVGL v8.3.11 集成落地，四项验收（窗口可见 / 点击有响应 / 关窗干净退出 / 补丁版号入 `config/`）全部有证据；构建零警告、`ctest` 全绿。下面按"交付物 / 定稿契约 / 关键决定 / 验收证据"记录。

### 交付物

| 区域 | 文件 |
| --- | --- |
| 依赖 | `third_party/lvgl` submodule 固定 **v8.3.11**（gitlink `74d0a816a440eea53e030c4f1af842a94f7ce3d3`）+ `.gitmodules` 一行 |
| 配置 | `config/lv_conf.h`（自持）、`config/embark_lvgl_hooks.h`（C 钩子声明：alloc/free/realloc/assert）、`config/embark_limits.h`（显示尺寸、RGB565、绘制缓冲行数、LVGL 堆预算、输入队列深度） |
| 构建 | `cmake/embark_sdl2.cmake`（新）、`third_party/CMakeLists.txt`（`LV_CONF_PATH` + `add_subdirectory(lvgl)` + `embark_lvgl`）、顶层 `CMakeLists.txt`、`platform/host/CMakeLists.txt`（基础库 / UI 库 / 可执行三层）、`cmake/embark_middleware.cmake`（注释说明 LVGL 不进视图） |
| 宿主后端 | `host_display.{h,cpp}`、`host_input.{h,cpp}`、`lvgl_port.{h,cpp}`、`host_lvgl_mem.{h,cpp}`、`ui_demo.cpp`（可执行 `embark_host_ui`）、`host_context.{h,cpp}`（`attach_display`/`attach_input`） |
| 内核 / 测试 | `include/embark/detail/allocation_counter.h`、`tests/detail/test_allocation_counter.cpp`、`tests/CMakeLists.txt`、`include/embark/hal/types.h`（输入事件 key 约定） |
| 证据 | 窗口截图（点击前后全图 + 局部裁剪）。PNG 已从仓库移除 —— 二进制证据不再入库 |

### 定稿契约

- **单一像素格式 RGB565**（`LV_COLOR_DEPTH 16`、无 swap）：显示与输入后端只做这一种，别的格式返回 `unsupported`。`IDisplay::flush` 的数据是**紧凑**布局（pitch = 区域宽 × 2）；背光 100% 直通零拷贝，<100% 走逐通道缩放暂存区（按需增长，`no_space` 只在分配失败时出现）。
- **输入事件 key 约定**（写进 `hal/types.h`）：**指针事件 key 恒为 0；按键事件 key 非 0**（0..255，此时 x/y 只是"按键那一刻指针在哪"）。宿主鼠标的 button 号不占 key —— 用 `1` 当键号会让上层把点击当成键盘事件整条丢掉（第一轮验收就是这么失败的）。v1 只有 POINTER indev，key≠0 的事件计入 `ignored_key_events()`（keypad 归 issue 12）。
- **退出信号走输入后端**：`SDL_QUIT` / `SDL_WINDOWEVENT_CLOSE` → `HostInput::quit_requested()`。SDL 只有一个事件队列、泵在 `poll()` 里，所以 UI 循环必须把队列抽干（`pump_input()` 内部 `while (SDL_PollEvent(...))`），否则后面的事件永远读不到。
- **LVGL 内存**：`LV_MEM_CUSTOM 1` + 自己的 `embark_lvgl_alloc/free/realloc`（记账 + 预算 `lvgl_alloc_budget_bytes = 256 KB`）。超预算或分配失败返回 nullptr，由 `LV_USE_ASSERT_MALLOC` 带**分配点文件行号**进 `embark_lvgl_assert_failed()` → `embark::fatal` —— 报错里有调用点，这就是"不在这里 abort"的原因。**`LV_MEM_CUSTOM=1` 时上游没有 `lv_deinit`**（`lv_obj.h:206-214` 的门是 `LV_ENABLE_GC || !LV_MEM_CUSTOM`），所以 `LvglPort` 析构只断日志出口、清标志；想看 LVGL 是否还干净，要在析构**之前**调 `lvgl_outstanding_bytes()`（演示程序正是这么做的）。
- **LVGL 日志**：`lv_log_register_print_cb` → HAL `ILogSink`（该回调没有 user_data，用文件级 sink 指针转交，前提是"一个进程一份 LVGL"，见 spec §8）；`LV_LOG_LEVEL_WARN`、`LV_LOG_PRINTF 0`。
- **绘制缓冲**：文件级静态 `etl::array<lv_color_t, 40 × 320>` = 25 KB 进 `.bss`（地址必须终身不变，`alignas(4)` 为真机 DMA 留样子）。
- **构建开关**：找不到 SDL2（CONFIG 模式）只跳过 `embark_host_ui` 并打一行 STATUS，内核/后端/测试照常构建。

### 关键决定

1. **LVGL 不进 middleware 视图**（对已批准方案的有意偏离 ①）：视图的唯一理由是满足上游写死的 `<middleware/...>` 互相引用，而 LVGL 按 `lvgl.h` + `LV_LVGL_H_INCLUDE_SIMPLE` 引入；再造一条 junction 只会多出第二套写法。
2. **演示界面文案用英文**（有意偏离 ②）：LVGL 内置只有 Montserrat，没有中文字形；中文界面要配自定义字体（`LV_FONT_CUSTOM_DECLARE` + 字库），归 issue 12。
3. `lv_conf.h` **只钉影响内存/尺寸/构图/可观测性的项**，其余留给 `lv_conf_internal.h` 的 `#ifndef` 默认（部分配置合法、不刷警告）。两处硬约束：头护栏必须叫 `LV_CONF_H`（否则每个 TU 刷 `Possible failure to include lv_conf.h`）；`LV_ASSERT_HANDLER` 宏必须自带结尾分号（`lv_assert.h` 的展开是裸语句）。
4. **补丁版定在 v8.3.11**（spec §15 的待定项消掉）：submodule + gitlink 固定，`config/` 里不再留"版号待定"。
5. **截图读回顺序**：`SDL_RenderReadPixels` 读的是后备缓冲，Present 之后其内容在 flip 模型下未定义 —— 第一版截图有一大片没画上的黑区。改为 `HostDisplay::capture_surface()`：整屏重画 → **Present 之前**读数 → 再 Present（纹理里始终存着整帧，每个脏区都写进同一张纹理）。
6. **验收走真链路**：`--click` / `--quit-at` 用 `SDL_PushEvent` 把事件塞进 SDL 自己的队列，等于"人在窗口上点了一下 / 点了 ×"，不是直接调 HAL 或回调。退出码 2 = 点了按钮但回调没触发（验收失败）。
7. `embark_find_sdl2` 是 **macro** 而不是 function：`find_package(SDL2 CONFIG)` 给出的 `SDL2_BINDIR` 等普通变量必须落在调用方的目录作用域（function 会开自己的作用域把它们丢掉）。

### 验收证据

- **构建**：`cmake --build build` 零警告（212 目标）；产出 `liblvgl.a`、`libembark_platform_host[_ui].a`、`embark_host_ui.exe`、`SDL2.dll`（POST_BUILD 从 MinGW 目录拷到可执行文件旁）。
- **测试**：`ctest --test-dir build --output-on-failure` → `1/1 Passed`（新增 6 个 `AllocationCounter` 用例：累计/峰值、释放保留峰值、realloc 差值、释放对不上夹 0、`over_budget` 严格大于、reset）。
- **窗口可见**：`embark_host_ui.exe --frames 150 --screenshot ui.bmp` → 640×480（面板 320×240 × 放大 2×），背景 `(16,24,32)`、按钮 `LV_PALETTE_BLUE` 经 RGB565 量化为 `(32,149,246)`、文字灰阶齐全，整帧无未画区域（证据 PNG 见上表）。
- **点击有响应**：`--frames 40 --click` exit 0，日志 `按钮点击：第 1 次`，LVGL 刷新 11 次 / 214164 字节 / 丢输入 0；**150 帧的"无点击 vs 有点击"两张截图逐像素比对，差异只剩 bbox (264,164)-(378,186)** —— 正是 `Clicks: 0` → `Clicks: 1` 那一行字。（40 帧那组还能看到按钮余留色差：那是默认主题 80 ms 的颜色过渡动画，150 帧时两端已一致。）
- **关窗干净退出**：`--quit-at 30` → 日志 `收到关窗请求：第 31 帧，开始收尾` + exit 0。
- **LVGL 内存**：分配 161 次、峰值 9333 字节、退出前未回收 6741 字节（预算 262144，用掉约 3.5%）。
- **LVGL 只被 main 循环调用**：`lv_timer_handler()` 出现在 `ui_demo.cpp` 主循环里，`LvglPort` 自己没有定时器；issue 06 接手后改由唯一 UI 任务调用。

