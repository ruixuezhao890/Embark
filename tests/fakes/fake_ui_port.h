// FakeUiPort：IUiPort 的调用记录（init/tick/pump/process/shutdown 次数 + 可控退出标志）。
// 与 FakeHal 同一哲学：能数调用、能预设失败/退出，其余什么都不做。
#pragma once

#include <embark/ui_port.h>

namespace embark::fakes {

class FakeUiPort final : public IUiPort {
 public:
  Error init() override { ++init_count; return init_result; }
  void tick() noexcept override { ++tick_count; }
  void pump_input() noexcept override { ++pump_count; }
  void process() noexcept override { ++process_count; }
  bool exit_requested() const noexcept override { return exit_requested_value; }
  void shutdown() noexcept override { ++shutdown_count; }

  int init_count = 0;
  int tick_count = 0;
  int pump_count = 0;
  int process_count = 0;
  int shutdown_count = 0;

  Error init_result = Error::none;
  bool exit_requested_value = false;  ///< 设为 true 模拟"用户关了窗"
};

}  // namespace embark::fakes