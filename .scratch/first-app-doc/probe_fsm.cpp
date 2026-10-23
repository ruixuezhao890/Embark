#include <cstdint>
#include <cstdio>

#include <middleware/etl/fsm.h>

#include <embark/message.h>

using embark::MessageT;

// 三个消息（id 0x30..0x32；0xFE 框架保留给跨任务信封，勿撞）
struct StartMessage : public MessageT<0x30> {
  constexpr StartMessage(std::uint32_t t = 0) noexcept : now_ms(t) {}
  std::uint32_t now_ms = 0;  // 按下那一刻的时刻，由 App 填
};
struct PauseMessage : public MessageT<0x31> {
  constexpr PauseMessage(std::uint32_t t = 0) noexcept : now_ms(t) {}
  std::uint32_t now_ms = 0;
};
struct ResetMessage : public MessageT<0x32> {
  constexpr ResetMessage() noexcept = default;
};

// 状态 id 必须 0..N-1 且按声明顺序（fsm_state_pack 的编译期 static_assert）
enum StateId : etl::fsm_state_id_t { kIdle = 0, kRunning = 1, kPaused = 2 };

class StopwatchFsm;

class StateIdle : public etl::fsm_state<StopwatchFsm, StateIdle, kIdle, StartMessage, ResetMessage> {
 public:
  etl::fsm_state_id_t on_event(const StartMessage& msg);
  etl::fsm_state_id_t on_event(const ResetMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

class StateRunning : public etl::fsm_state<StopwatchFsm, StateRunning, kRunning, StartMessage,
                                           PauseMessage, ResetMessage> {
 public:
  etl::fsm_state_id_t on_event(const StartMessage& msg);
  etl::fsm_state_id_t on_event(const PauseMessage& msg);
  etl::fsm_state_id_t on_event(const ResetMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

class StatePaused : public etl::fsm_state<StopwatchFsm, StatePaused, kPaused, StartMessage,
                                          PauseMessage, ResetMessage> {
 public:
  etl::fsm_state_id_t on_event(const StartMessage& msg);
  etl::fsm_state_id_t on_event(const PauseMessage& msg);
  etl::fsm_state_id_t on_event(const ResetMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

// context：状态机自己就是"状态 + 数据"（数据放 context，不放状态类）
class StopwatchFsm final : public etl::fsm {
 public:
  StopwatchFsm() noexcept : etl::fsm(kIdle) { set_states(states_); }

  void start_at(std::uint32_t now_ms) noexcept {
    started_ms_ = now_ms;
    running_ = true;
  }
  void pause_at(std::uint32_t now_ms) noexcept {
    if (running_) base_ms_ += now_ms - started_ms_;
    running_ = false;
  }
  void clear() noexcept {
    base_ms_ = 0;
    running_ = false;
  }
  [[nodiscard]] std::uint32_t value(std::uint32_t now_ms) const noexcept {
    return running_ ? base_ms_ + (now_ms - started_ms_) : base_ms_;
  }

 private:
  etl::fsm_state_pack<StateIdle, StateRunning, StatePaused> states_;
  std::uint32_t base_ms_ = 0;
  std::uint32_t started_ms_ = 0;
  bool running_ = false;
};

// 状态类的成员函数体放 context 之后：get_fsm_context() 需要完整类型
etl::fsm_state_id_t StateIdle::on_event(const StartMessage& msg) {
  get_fsm_context().start_at(msg.now_ms);
  return kRunning;
}
etl::fsm_state_id_t StateIdle::on_event(const ResetMessage&) { return No_State_Change; }
etl::fsm_state_id_t StateIdle::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StateIdle::on_enter_state() { return No_State_Change; }

etl::fsm_state_id_t StateRunning::on_event(const StartMessage&) { return No_State_Change; }
etl::fsm_state_id_t StateRunning::on_event(const PauseMessage& msg) {
  get_fsm_context().pause_at(msg.now_ms);
  return kPaused;
}
etl::fsm_state_id_t StateRunning::on_event(const ResetMessage&) {
  get_fsm_context().clear();
  return kIdle;
}
etl::fsm_state_id_t StateRunning::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StateRunning::on_enter_state() { return No_State_Change; }

etl::fsm_state_id_t StatePaused::on_event(const StartMessage& msg) {
  get_fsm_context().start_at(msg.now_ms);
  return kRunning;
}
etl::fsm_state_id_t StatePaused::on_event(const PauseMessage&) { return No_State_Change; }
etl::fsm_state_id_t StatePaused::on_event(const ResetMessage&) {
  get_fsm_context().clear();
  return kIdle;
}
etl::fsm_state_id_t StatePaused::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StatePaused::on_enter_state() { return No_State_Change; }

int main() {
  StopwatchFsm fsm;
  fsm.start();
  fsm.receive(StartMessage{1000U});
  std::printf("start   state=%d value=%u\n", static_cast<int>(fsm.get_state_id()), fsm.value(1500U));
  fsm.receive(PauseMessage{1500U});
  std::printf("pause   state=%d value=%u\n", static_cast<int>(fsm.get_state_id()), fsm.value(9999U));
  fsm.receive(StartMessage{2000U});
  std::printf("resume  state=%d value=%u\n", static_cast<int>(fsm.get_state_id()), fsm.value(2300U));
  fsm.receive(ResetMessage{});
  std::printf("reset   state=%d value=%u\n", static_cast<int>(fsm.get_state_id()), fsm.value(9999U));
  return 0;
}
