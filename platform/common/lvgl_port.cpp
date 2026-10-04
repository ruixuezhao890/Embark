/**
 * 平台共用层 · LVGL 端口实现（issues/05、11）
 *
 * 见 lvgl_port.h 的说明。这里补两条实现上的取舍：
 *
 *   - 绘制缓冲是文件级静态（40 行 × 320 像素 × 2 字节 = 25 KB，进 .bss），
 *     不进任何堆：它必须在 LVGL 整个生命周期里保持同一个地址，且真机上要落在
 *     内部 SRAM（LVGL 的 flush 是逐区同步的，缓冲本身不必支持 DMA）。
 *   - LVGL 的日志回调没有 user_data（lv_log_print_cb 只收一个 const char*），
 *     所以用文件级 sink 指针转交 —— v1 规定一个进程只有一份 LVGL、一份 HAL
 *     （spec §8），这条捷径不成立的时候才会需要重构。
 */
#include "lvgl_port.h"

#include <cstddef>
#include <cstdint>

#include <middleware/etl/array.h>
#include <middleware/etl/span.h>
#include <middleware/elog/elog.hpp>

namespace embark::platform {
namespace {

/// LVGL 的绘制缓冲。alignas(4) 为真机（SPI/DMA 要 4 字节对齐）留好样子。
alignas(4) etl::array<lv_color_t, lvgl_draw_buf_lines * display_width> g_draw_buffer;

/// LVGL 日志出口。init() 时指向 context.log；lv_log_print_cb 没有 user_data，
/// 只能这样转交（见文件头说明）。
hal::ILogSink* g_log_sink = nullptr;

}  // namespace

LvglPort::LvglPort(hal::Context& context) noexcept : context_(context) {}

LvglPort::~LvglPort() noexcept {
  if (ready_) {
    // 这里**不能**调 lv_deinit()：它只在 `#if LV_ENABLE_GC || !LV_MEM_CUSTOM` 里声明
    // （third_party/lvgl/src/core/lv_obj.h:206-214），而我们的 config/lv_conf.h 是
    // LV_MEM_CUSTOM=1 —— LVGL 用别人的分配器时没法自己把全局状态还干净，上游干脆不提供。
    // 于是析构只断开日志出口、清就绪标志，对象由进程退出统一回收。
    // 想知道"活着的 LVGL 占了多少"，在析构之前调 embark_lvgl_outstanding_bytes()。
    g_log_sink = nullptr;
    ready_ = false;
  }
}

Error LvglPort::init() noexcept {
  if (ready_) {
    return Error::none;
  }
  if (context_.display == nullptr || !context_.display->is_ready()) {
    return Error::not_ready;  // 没有可用显示设备就没有 LVGL 端口
  }

  const hal::DisplayInfo info = context_.display->info();
  if (info.width == 0U || info.height == 0U) {
    return Error::not_ready;
  }
  if (info.format != hal::PixelFormat::rgb565) {
    // 单一像素格式是 issue 05 拍板的结果（config/lv_conf.h 的 LV_COLOR_DEPTH 16）。
    // 换成别的格式要在这里加转换层，v1 故意不做。
    return Error::unsupported;
  }
  if (static_cast<std::size_t>(info.width) > display_width ||
      static_cast<std::size_t>(info.height) > display_height) {
    return Error::invalid_argument;  // 绘制缓冲是按 config/embark_limits.h 定死的
  }

  lv_init();

  lv_disp_draw_buf_init(&draw_buffer_, g_draw_buffer.data(), nullptr,
                        static_cast<std::uint32_t>(g_draw_buffer.size()));

  lv_disp_drv_init(&display_driver_);
  display_driver_.hor_res = info.width;
  display_driver_.ver_res = info.height;
  display_driver_.draw_buf = &draw_buffer_;
  display_driver_.flush_cb = &LvglPort::flush_thunk;
  display_driver_.user_data = this;
  if (lv_disp_drv_register(&display_driver_) == nullptr) {
    return Error::io_failure;
  }

  if (context_.input != nullptr) {
    lv_indev_drv_init(&input_driver_);
    input_driver_.type = LV_INDEV_TYPE_POINTER;
    input_driver_.read_cb = &LvglPort::read_thunk;
    input_driver_.user_data = this;
    if (lv_indev_drv_register(&input_driver_) == nullptr) {
      return Error::io_failure;
    }
  }

  g_log_sink = &context_.log;
  lv_log_register_print_cb(&LvglPort::log_thunk);  // config/lv_conf.h 里只放 WARN 及以上

  last_tick_ms_ = context_.time.now_ms();
  ready_ = true;
  return Error::none;
}

void LvglPort::tick() noexcept {
  const std::uint32_t now = context_.time.now_ms();
  const std::uint32_t delta = now - last_tick_ms_;  // 无符号减法：回绕发生在 49.7 天之后，天然正确
  last_tick_ms_ = now;
  if (delta != 0U) {
    lv_tick_inc(delta);
  }
}

void LvglPort::pump_input() noexcept {
  if (context_.input == nullptr) {
    return;
  }
  for (;;) {
    hal::InputEvent event;
    const etl::expected<bool, Error> result = context_.input->poll(event);
    if (!result.has_value()) {
      if (!input_error_reported_) {
        input_error_reported_ = true;
        ELOG_ERROR("输入后端读取失败：{}（后续失败不再重复报告）", result.error());
      }
      return;
    }
    if (!result.value()) {
      return;  // 队列抽干了
    }
    if (events_.full()) {
      ++dropped_input_events_;  // 满了：push 会覆盖队首（丢最旧），这里把它记下来
    }
    events_.push(event);
  }
}

void LvglPort::flush_area(const lv_area_t& area, const lv_color_t* pixels) noexcept {
  const hal::Rect rect{area.x1, area.y1, area.x2 - area.x1 + 1, area.y2 - area.y1 + 1};
  const std::size_t byte_count = hal::area_bytes(rect, hal::PixelFormat::rgb565);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(pixels);

  ++refreshes_;
  const Error error =
      context_.display->flush(rect, etl::span<const std::uint8_t>(bytes, byte_count));
  if (error == Error::none) {
    flush_bytes_ += static_cast<std::uint32_t>(byte_count);
    return;
  }
  if (!flush_error_reported_) {
    flush_error_reported_ = true;
    ELOG_ERROR("显示刷新失败：{} 区域 ({},{},{}×{})（后续失败不再重复报告）", error, rect.x, rect.y,
               rect.width, rect.height);
  }
}

void LvglPort::read_event(lv_indev_data_t& data) noexcept {
  if (events_.empty()) {
    // 队列空了也要如实上报"按键还按着"，否则 LVGL 会把按住不放当成松开。
    data.point.x = pointer_x_;
    data.point.y = pointer_y_;
    data.state = pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    return;
  }

  const hal::InputEvent event = events_.front();
  events_.pop();

  switch (event.kind) {
    case hal::InputEventKind::press:
      if (event.key != 0U) {
        // 非 0 的 key = 按键事件（hal/types.h 的约定）；v1 的 indev 只有 POINTER，键盘归 issue 12。
        ++ignored_key_events_;
        break;
      }
      pressed_ = true;
      pointer_x_ = event.x;
      pointer_y_ = event.y;
      break;
    case hal::InputEventKind::release:
      if (event.key != 0U) {
        ++ignored_key_events_;
        break;
      }
      pressed_ = false;  // 坐标保留：LVGL 按下与抬起之间要靠它算点击位置
      break;
    case hal::InputEventKind::move:
      pointer_x_ = event.x;
      pointer_y_ = event.y;
      break;
  }

  data.point.x = pointer_x_;
  data.point.y = pointer_y_;
  data.state = pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  // 还有事件就请 LVGL 再读一次：lv_indev_read_timer_cb 是 do/while(continue_reading)
  // （lv_indev.c:81-114），于是每次按下/抬起/移动都会被单独处理，不会被后面的移动挤掉。
  data.continue_reading = !events_.empty();
}

void LvglPort::flush_thunk(lv_disp_drv_t* driver, const lv_area_t* area,
                           lv_color_t* pixels) noexcept {
  if (driver != nullptr && area != nullptr && pixels != nullptr) {
    auto* port = static_cast<LvglPort*>(driver->user_data);
    if (port != nullptr) {
      port->flush_area(*area, pixels);
    }
  }
  if (driver != nullptr) {
    lv_disp_flush_ready(driver);  // 同步刷新：立刻把缓冲还给 LVGL
  }
}

void LvglPort::read_thunk(lv_indev_drv_t* driver, lv_indev_data_t* data) noexcept {
  if (driver == nullptr || data == nullptr) {
    return;
  }
  auto* port = static_cast<LvglPort*>(driver->user_data);
  if (port != nullptr) {
    port->read_event(*data);
  }
}

void LvglPort::log_thunk(const char* message) noexcept {
  if (message == nullptr || g_log_sink == nullptr) {
    return;
  }
  // LVGL 的记录自带级别前缀与结尾换行（src/misc/lv_log.c），原样交出去：
  // 这里不是 elog 的记录，不走 elog 的格式化，只借同一个 sink 出口。
  const char* end = message;
  while (*end != '\0') {
    ++end;
  }
  g_log_sink->write(message, static_cast<std::size_t>(end - message));
}

}  // namespace embark::platform
