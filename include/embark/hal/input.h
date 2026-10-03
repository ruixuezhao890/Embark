/**
 * HAL · 输入（spec §8）
 *
 * poll() 而不是"回调注册"：v1 只有 UI 任务碰输入（spec §5 的单一 UI 任务），
 * 轮询天然把事件顺序和渲染顺序绑在同一条时间线上，不需要跨任务的回调队列。
 */
#ifndef EMBARK_HAL_INPUT_H
#define EMBARK_HAL_INPUT_H

#include <embark/error.h>
#include <embark/hal/types.h>
#include <middleware/etl/expected.h>

namespace embark::hal {

class IInput {
 public:
  virtual ~IInput() = default;

  virtual Error init() noexcept = 0;

  /// true = 取到一个事件；false = 现在没有（不是错误）；Error = 设备真的坏了。
  /// 一次只取一个：调用方在自己的节拍里循环到 false 为止。
  [[nodiscard]] virtual etl::expected<bool, Error> poll(InputEvent& event) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_INPUT_H */
