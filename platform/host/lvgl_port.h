/**
 * 宿主 · LVGL 端口（issues/05）
 *
 * 把 LVGL 接到 HAL 上，四件事：
 *   显示 —— LVGL 的 flush_cb → hal::IDisplay::flush。LVGL 给的是闭区间坐标
 *           （x1..x2、y1..y2），HAL 要的是 {x, y, width, height}，加一时别忘；
 *           LVGL 的缓冲区是紧凑行（pitch = 区域宽 × 2），与 HAL 契约一致，零拷贝直通。
 *   输入 —— 先由 pump_input() 把 HAL 输入队列抽干、投进内部定长队列（抽干是必须的：
 *           LVGL 只在 lv_timer_handler() 里读输入，队列不清空就会一直读到旧事件）。
 *           read_cb 每次只取一个事件，取完若还有就置 continue_reading ——
 *           LVGL 8.3 的 `lv_indev_read_timer_cb` 是 `do { ... } while(continue_reading)`
 *           （lv_indev.c:81-114），所以一次点击/一次拖动都不会被后面的移动事件挤掉。
 *   时间 —— tick() 读 hal::ITime::now_ms() 与上次求差，喂 lv_tick_inc()（LV_TICK_CUSTOM 0）。
 *   日志/断言 —— lv_log_register_print_cb → ILogSink；LVGL 断言在
 *           config/lv_conf.h 里指到 embark_lvgl_assert_failed()（host_lvgl_mem.cpp）→ fatal。
 *
 * 内存：config/lv_conf.h 的 LV_MEM_CUSTOM 把 LVGL 的分配全部指到 embark_lvgl_*，
 * 于是"LVGL 用了多少"可测（platform::host::lvgl_peak_bytes()，见 host_lvgl_mem.h）。
 *
 * 线程：LVGL 只在一条线上跑。issue 05 这条线是 main，issue 06 起交给唯一的 UI 任务
 * （spec §5）—— 本类不含任何锁，也不该有。
 */
#ifndef EMBARK_PLATFORM_HOST_LVGL_PORT_H
#define EMBARK_PLATFORM_HOST_LVGL_PORT_H

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/hal/types.h>
#include <embark_limits.h>
#include <middleware/etl/circular_buffer.h>

namespace embark::platform::host {

class LvglPort {
 public:
  /// 引用在对象存活期间必须有效（宿主是进程级静态，没问题）。
  explicit LvglPort(hal::Context& context) noexcept;
  ~LvglPort() noexcept;

  LvglPort(const LvglPort&) = delete;
  LvglPort& operator=(const LvglPort&) = delete;

  /// lv_init + 注册显示/输入驱动。要求 context.display 已就绪（context.input 可空：
  /// 无输入的平台只有显示）。可重复调用：已就绪时直接返回 none。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] bool is_ready() const noexcept { return ready_; }

  /// 推进 LVGL 时间轴。UI 循环每帧一次，必须在 lv_timer_handler() 之前。
  void tick() noexcept;

  /// 把 HAL 输入队列抽干投进内部队列（本类不直接读 HAL —— 保证与 UI 循环同一条节拍）。
  void pump_input() noexcept;

  /// 观测：验收与看板用。
  [[nodiscard]] std::uint32_t refreshes() const noexcept { return refreshes_; }
  [[nodiscard]] std::uint32_t flush_bytes() const noexcept { return flush_bytes_; }
  [[nodiscard]] std::size_t pending_events() const noexcept { return events_.size(); }
  [[nodiscard]] std::size_t dropped_input_events() const noexcept { return dropped_input_events_; }
  [[nodiscard]] std::size_t ignored_key_events() const noexcept { return ignored_key_events_; }
  /// 内部输入队列的容量（做压力/溢出判断时用）。
  [[nodiscard]] static constexpr std::size_t input_queue_capacity() noexcept {
    return lvgl_input_queue_depth;
  }

 private:
  static void flush_thunk(lv_disp_drv_t* driver, const lv_area_t* area,
                          lv_color_t* pixels) noexcept;
  static void read_thunk(lv_indev_drv_t* driver, lv_indev_data_t* data) noexcept;
  static void log_thunk(const char* message) noexcept;

  void flush_area(const lv_area_t& area, const lv_color_t* pixels) noexcept;
  void read_event(lv_indev_data_t& data) noexcept;

  hal::Context& context_;

  // LVGL 存的是这些结构的地址（lv_disp_drv_register / lv_indev_drv_register），
  // 所以在 LvglPort 的整个生命周期里不能搬家 —— 因此是成员而不是局部变量。
  lv_disp_drv_t display_driver_{};
  lv_indev_drv_t input_driver_{};
  lv_disp_draw_buf_t draw_buffer_{};

  etl::circular_buffer<hal::InputEvent, lvgl_input_queue_depth> events_;

  std::uint32_t last_tick_ms_ = 0;
  std::uint32_t refreshes_ = 0;
  std::uint32_t flush_bytes_ = 0;
  std::uint16_t pointer_x_ = 0;
  std::uint16_t pointer_y_ = 0;
  std::size_t dropped_input_events_ = 0;
  std::size_t ignored_key_events_ = 0;
  /// 按键当前是否按住：队列空了也要如实上报，否则 LVGL 会在下一个读周期把
  /// "按住不放"当成"已松开"（真实触摸屏每个周期都会重报当前状态）。
  bool pressed_ = false;
  bool ready_ = false;
  bool flush_error_reported_ = false;
  bool input_error_reported_ = false;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_LVGL_PORT_H */
