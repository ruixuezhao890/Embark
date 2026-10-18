#include "host_audio.h"

#include <SDL.h>

#include <cstring>

#include <middleware/elog/elog.hpp>

namespace embark::platform::host {
namespace {

/// SDL 要的是「每声道 16 bit、双声道、48000 Hz」—— 与 wav_player.h 的输出口径一致。
constexpr int kSdlBufferFrames = 1024;

}  // namespace

HostAudio::~HostAudio() {
  lock();
  player_.close();
  unlock();
  if (device_ != 0U) {
    SDL_CloseAudioDevice(device_);
    device_ = 0U;
  }
  if (sdl_ready_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdl_ready_ = false;
  }
}

void HostAudio::lock() noexcept {
  while (lock_.test_and_set(std::memory_order_acquire)) {
    // 自旋：临界区只有几次 fread + 一次乘法，等的时间远小于一次上下文切换。
  }
}

void HostAudio::unlock() noexcept {
  lock_.clear(std::memory_order_release);
}

Error HostAudio::init() noexcept {
  if (device_ != 0U) {
    return Error::none;
  }

  if (!sdl_ready_) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
      ELOG_WARN("音频：SDL 音频子系统起不来（{}）—— 本机没有可用的音频输出", SDL_GetError());
      return Error::unsupported;
    }
    sdl_ready_ = true;
  }

  SDL_AudioSpec want{};
  want.freq = static_cast<int>(audio::kOutputSampleRate);
  want.format = AUDIO_S16SYS;
  want.channels = static_cast<Uint8>(audio::kOutputChannels);
  want.samples = static_cast<Uint16>(kSdlBufferFrames);
  want.callback = &HostAudio::sdl_callback;
  want.userdata = this;

  SDL_AudioSpec have{};
  device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  if (device_ == 0U) {
    ELOG_WARN("音频：SDL_OpenAudioDevice 失败（{}）—— 本机没有可用的音频输出", SDL_GetError());
    return Error::unsupported;
  }

  SDL_PauseAudioDevice(device_, 0);  // 0 = 开始要数据（设备空闲时回调照样被喂零）
  opened_ = true;
  ELOG_INFO("音频：SDL 设备就绪（{} Hz / {} 声道 / 缓冲 {} 帧）", have.freq, want.channels,
            have.samples);
  return Error::none;
}

Error HostAudio::play(const char* path) noexcept {
  if (device_ == 0U) {
    return Error::not_ready;
  }
  lock();
  const Error opened = player_.open(path);
  Error started = opened;
  if (started == Error::none) {
    started = player_.play();
  }
  unlock();
  if (started != Error::none) {
    ELOG_WARN("音频：播放 {} 失败（{}）", path, started);
    return started;
  }
  ELOG_INFO("音频：开始播放 {}（{} ms）", path, duration_ms());
  return Error::none;
}

Error HostAudio::pause() noexcept {
  lock();
  const Error result = player_.pause();
  unlock();
  return result;
}

Error HostAudio::resume() noexcept {
  lock();
  const Error result = player_.resume();
  unlock();
  return result;
}

Error HostAudio::stop() noexcept {
  lock();
  const Error result = player_.stop();
  unlock();
  return result;
}

Error HostAudio::seek(std::uint32_t position_ms) noexcept {
  lock();
  const Error result = player_.seek(position_ms);
  unlock();
  return result;
}

hal::AudioState HostAudio::state() const noexcept {
  const_cast<HostAudio*>(this)->lock();
  const hal::AudioState state = player_.state();
  const_cast<HostAudio*>(this)->unlock();
  return state;
}

std::uint32_t HostAudio::position_ms() const noexcept {
  const_cast<HostAudio*>(this)->lock();
  const std::uint32_t position = player_.position_ms();
  const_cast<HostAudio*>(this)->unlock();
  return position;
}

std::uint32_t HostAudio::duration_ms() const noexcept {
  const_cast<HostAudio*>(this)->lock();
  const std::uint32_t duration = player_.duration_ms();
  const_cast<HostAudio*>(this)->unlock();
  return duration;
}

Error HostAudio::set_volume(std::uint8_t percent) noexcept {
  lock();
  const Error result = player_.set_volume(percent);
  unlock();
  return result;
}

void HostAudio::sdl_callback(void* userdata, std::uint8_t* stream, int len) noexcept {
  auto* self = static_cast<HostAudio*>(userdata);
  if (self == nullptr || stream == nullptr || len <= 0) {
    return;
  }

  const std::size_t frame_bytes = sizeof(std::int16_t) * audio::kOutputChannels;
  const std::size_t wanted = static_cast<std::size_t>(len) / frame_bytes;

  self->lock();
  const std::size_t produced =
      self->player_.read_stereo(reinterpret_cast<std::int16_t*>(stream), wanted);
  self->unlock();

  if (produced < wanted) {
    // 没在播 / 播完了：剩下的补静音（设备一直在要数据，不补就是上一轮的残响）。
    std::memset(stream + (produced * frame_bytes), 0,
                static_cast<std::size_t>(len) - (produced * frame_bytes));
  }
}

}  // namespace embark::platform::host
