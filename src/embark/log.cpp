#include <embark/log.h>

namespace embark {

bool LogSinkBinder::write_thunk(const char* data, std::size_t size, void* user_data) noexcept {
  auto* self = static_cast<LogSinkBinder*>(user_data);
  if (self == nullptr || self->sink_ == nullptr) {
    return false;
  }

  self->append(data, size);

  // 返回值 elog 不看（sink 写失败不回溯），但契约上要报告状态。
  return true;
}

void LogSinkBinder::append(const char* data, std::size_t size) noexcept {
  if (data == nullptr) {
    return;
  }

  for (std::size_t i = 0; i < size; ++i) {
    if (pending_length_ < pending_.size()) {
      pending_[pending_length_] = data[i];
      ++pending_length_;
    }

    if (data[i] == '\n') {
      // 整行齐了：一次加锁、一次下发。
      emit(pending_.data(), pending_length_);
      pending_length_ = 0;
    } else if (pending_length_ == pending_.size()) {
      // 缓冲满且没有换行 —— 与其丢字，不如先把这段送出去（正常记录不会走到这里：
      // elog 放不下的整行会自己丢掉，elog.hpp:211/218）。
      emit(pending_.data(), pending_length_);
      pending_length_ = 0;
    }
  }
}

void LogSinkBinder::emit(const char* data, std::size_t size) noexcept {
  if (sink_ == nullptr || data == nullptr || size == 0) {
    return;
  }

#if EMBARK_LOG_SERIALIZE
  const etl::lock_guard<etl::mutex> guard(mutex_);
#endif

  sink_->write(data, size);
  sink_->flush();  // 让每行当场可见/落盘：崩溃前的最后一行最值钱
  ++lines_;
  bytes_ += size;
}

e_log::logger* install_logger(const char* name, LogSinkBinder& binder,
                              e_log::level level) noexcept {
  return e_log::create_logger(name, binder.make_sink(), level);
}

}  // namespace embark
