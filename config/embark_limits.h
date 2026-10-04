/**
 * Embark 固定容量上限（spec §10）
 *
 * 内核零堆：所有容量都是编译期常量，都能在代码里查到，也都必须在这里集中列出。
 * 这些是 v1 的初值，落地 App/消息/定时器时按实测调整（issues/06、07）。
 */
#ifndef EMBARK_LIMITS_H
#define EMBARK_LIMITS_H

#include <cstddef>
#include <cstdint>

namespace embark {

// App 总数（静态注册表；注册顺序 = 默认前台顺序）
inline constexpr std::size_t max_apps = 8;

// 框架定时器数量（后台节拍用 etl::callback_timer<max_background_timers>）
inline constexpr std::size_t max_background_timers = 8;

// 一条消息队列的深度（跨任务单生产者单消费者）
inline constexpr std::size_t message_queue_depth = 16;

// --- HAL 持久化的容量（issues/04）--------------------------------------------
// 键值都走定长槽：宿主后端落文件、测试 fake 落内存，两边共用 detail/kv_slot.h 的编解码。
// 单条值上限故意给得小 —— 持久化只放"小对象"（校准值、上次页面、开关），大块数据不属于它。
inline constexpr std::size_t persistence_max_key_bytes = 16;
inline constexpr std::size_t persistence_max_value_bytes = 64;
inline constexpr std::size_t persistence_max_slots = 32;

// --- 显示（issues/04、05）----------------------------------------------------
// 全仓库只认一种像素格式：16 位 RGB565（与 config/lv_conf.h 的 LV_COLOR_DEPTH 一致）。
// 宿主窗口、LVGL 画布、目标 ST7789 三者都是这个尺寸，中间不做任何格式转换。
inline constexpr std::size_t display_width = 320;
inline constexpr std::size_t display_height = 240;
inline constexpr std::size_t display_bytes_per_pixel = 2;
inline constexpr std::size_t display_stride_bytes = display_width * display_bytes_per_pixel;

// --- LVGL（issue 05）---------------------------------------------------------
// 绘制缓冲：静态分配，行数 × 一行字节数就是它的占用（40 × 320 × 2 = 25 KB）。
// 行数越多一次能刷的带宽越大，越小则越省 SRAM；目标板上按 SRAM 预算调（issue 11）。
inline constexpr std::size_t lvgl_draw_buf_lines = 40;

// LVGL 堆预算：config/lv_conf.h 用 LV_MEM_CUSTOM=1 把 LVGL 的分配全部指到
// platform/host/host_lvgl_mem.cpp，超出这个数就进 fatal（spec §10 的"对象总量上限"）。
// 这是宿主值；目标板另配（仍是编译期常量，不引入运行时配置）。
inline constexpr std::size_t lvgl_alloc_budget_bytes = 256 * 1024;

// LVGL 端口的输入中转队列：UI 循环每帧把 HAL 输入抽干塞进来，LVGL 的 read_cb 再一个个取。
// 满了丢最旧并计数（与消息队列同一条纪律：宁可丢输入，不阻塞渲染）。
inline constexpr std::size_t lvgl_input_queue_depth = 16;

// --- 消息、总线与后台任务（issues/07）-------------------------------------------
// 总线的订阅者上限（= etl::message_bus 的 MAX_ROUTERS；镜像订阅表同容量）。
inline constexpr std::size_t max_bus_subscribers = 8;

// own_task 策略的后台任务数上限（每个 App 至多一个任务，槽位静态分配）。
inline constexpr std::size_t max_own_tasks = 2;

// 后台任务的默认栈深（单位：StackType_t 字；宿主 512 字 = 2 KB）。
// App 的 settings().task_stack_words 为 0 时用它；真机按 SRAM 预算重排（issue 11）。
inline constexpr std::size_t own_task_stack_words = 512;

// --- 唯一 UI 任务（issues/06）--------------------------------------------------
// 全工程只有一个 UI 任务（spec §6，宿主 = FreeRTOS 静态任务，真机同构）。
// 栈深单位是 StackType_t 字（宿主 4 字节）：2048 字 = 8 KB。留给 LVGL 回调 +
// 前台 App 逻辑；实测占用看宿主演示的栈高水位输出（uxTaskGetStackHighWaterMark），
// 真机按 SRAM 预算调（issue 11）。
inline constexpr std::size_t ui_task_stack_words = 2048;
// 优先级：高于内核空闲任务与其它系统任务即可；真机按中断/任务布局重排（issue 11）。
inline constexpr std::uint8_t ui_task_priority = 5;
// UI 循环的节拍（spec §6）：5 ms 一跳，空闲 vTaskDelay 让出、绝不忙等。
// 宿主墙钟约 2×（实测，见 spec §3），tick 语义不变。
inline constexpr std::uint32_t ui_loop_period_ms = 5;

// --- 测试替身的容量（tests/fakes/）-------------------------------------------
// 只是替身的缓冲上限，真机后端不读它；集中放这里是为了"容量都查得到"这条规矩不破例。
inline constexpr std::size_t fake_input_queue_depth = 8;
inline constexpr std::size_t fake_bus_script_depth = 8;
inline constexpr std::size_t fake_log_capture_bytes = 4096;

}  // namespace embark

#endif /* EMBARK_LIMITS_H */
