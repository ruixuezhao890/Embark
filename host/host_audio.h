/**
 * 宿主 · 音频服务（issue 09）：SDL2 音频设备 + 共用的 WAV 播放状态机
 *
 * 「把 PCM 喂给谁」在两头不同，所以这一层只剩接线：
 *   - 状态机、WAV 头解析、音量衰减、位置记账全在 core/common/wav_player.h（两侧共用）
 *   - 这里负责开 SDL 音频设备，并在 SDL 的回调里把 `read_stereo()` 的产物填进流
 *
 * 为什么用 SDL 回调而不是自己起一个喂数据的线程：回调本身就是「设备要数据了」这个事件，
 * 起线程就得自己算节拍、自己管缓冲水位，还多一个线程要关。回调里只做一次 memcpy 级的
 * 搬运 + 一次整数乘法（音量），没有锁竞争的热点。
 *
 * 回调跑在 SDL 自己的音频线程上，主线程同时可能调 pause()/set_volume() —— 两边都碰
 * WavPlayer，所以用一把自旋锁护住（不用 std::mutex：本接口的方法都是 noexcept，
 * 而 std::mutex::lock 会抛）。
 */
#ifndef EMBARK_PLATFORM_HOST_HOST_AUDIO_H
#define EMBARK_PLATFORM_HOST_HOST_AUDIO_H

#include <atomic>
#include <cstdint>

#include <embark/hal/audio.h>

#include "wav_player.h"

namespace embark::platform::host {

class HostAudio final : public hal::IAudio {
 public:
  HostAudio() noexcept = default;
  ~HostAudio() override;

  HostAudio(const HostAudio&) = delete;
  HostAudio& operator=(const HostAudio&) = delete;

  [[nodiscard]] Error init() noexcept override;

  [[nodiscard]] Error play(const char* path) noexcept override;
  [[nodiscard]] Error pause() noexcept override;
  [[nodiscard]] Error resume() noexcept override;
  [[nodiscard]] Error stop() noexcept override;
  [[nodiscard]] Error seek(std::uint32_t position_ms) noexcept override;
  [[nodiscard]] hal::AudioState state() const noexcept override;
  [[nodiscard]] std::uint32_t position_ms() const noexcept override;
  [[nodiscard]] std::uint32_t duration_ms() const noexcept override;
  [[nodiscard]] Error set_volume(std::uint8_t percent) noexcept override;

  /// 音频设备是否真的开起来了（`--audio-test` 用它决定要不要报「本机没有音频输出」）。
  [[nodiscard]] bool device_open() const noexcept { return device_ != 0U; }

 private:
  void lock() noexcept;
  void unlock() noexcept;

  /// SDL 音频回调：填满 len 字节（48 kHz / S16 / 双声道），不够就补静音。
  static void sdl_callback(void* userdata, std::uint8_t* stream, int len) noexcept;

  std::uint32_t device_ = 0;  ///< SDL_AudioDeviceID；0 = 没开成
  bool sdl_ready_ = false;    ///< 是否由我们 init 了 SDL 的 audio 子系统
  std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  mutable audio::WavPlayer player_{};
  bool opened_ = false;  ///< init() 是否成功过（play 之前必须为 true）
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_HOST_AUDIO_H */
