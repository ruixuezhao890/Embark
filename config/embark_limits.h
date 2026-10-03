/**
 * Embark 固定容量上限（spec §10）
 *
 * 内核零堆：所有容量都是编译期常量，都能在代码里查到，也都必须在这里集中列出。
 * 这些是 v1 的初值，落地 App/消息/定时器时按实测调整（issues/06、07）。
 */
#ifndef EMBARK_LIMITS_H
#define EMBARK_LIMITS_H

#include <cstddef>

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

// --- 测试替身的容量（tests/fakes/）-------------------------------------------
// 只是替身的缓冲上限，真机后端不读它；集中放这里是为了"容量都查得到"这条规矩不破例。
inline constexpr std::size_t fake_input_queue_depth = 8;
inline constexpr std::size_t fake_bus_script_depth = 8;
inline constexpr std::size_t fake_log_capture_bytes = 4096;

}  // namespace embark

#endif /* EMBARK_LIMITS_H */
