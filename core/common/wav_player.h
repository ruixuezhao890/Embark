/**
 * WAV（PCM）解析与播放状态机 —— 宿主与真机共用一份（issue 09）
 *
 * 为什么单独抽出来：两头「把 PCM 喂给谁」完全不同（宿主 = SDL 音频设备回调，
 * 真机 = I2S DMA 通道），但「读头 → 定位 data 块 → 按帧推进 → 音量 → 位置」是
 * 一模一样的一坨逻辑。写两遍必然漂移（一次改音量公式忘改另一边），而且状态机就
 * 没有一条能脱离声卡单测的路径了。
 *
 * 本类不认平台、不 include SDL、不 include IDF —— 只用 stdio。于是：
 *   - host/host_audio.cpp   = 本类 + SDL 设备（回调里 read_stereo）
 *   - device/src/esp32_audio.cpp = 本类 + I2S 通道（任务里 read_stereo）
 *   - core/tests/hal/test_audio.cpp       = 本类，喂一个自己写出来的小 WAV
 *
 * ## 输出口径固定：48000 Hz / 16 bit / 双声道交织
 *
 * 两个后端都按这个口径开硬件（I2S 的 slot 配置、SDL 的 spec 都写死它）。所以源文件
 * 必须也是 48 kHz —— 采样率转换是一个独立的、带插值算法的活儿，本票不做，
 * 非 48 kHz 的文件直接 `unsupported`。单声道源会被**复制到两个声道**（不是加混响）。
 */
#ifndef EMBARK_PLATFORM_COMMON_WAV_PLAYER_H
#define EMBARK_PLATFORM_COMMON_WAV_PLAYER_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <embark/error.h>
#include <embark/hal/audio.h>

namespace embark::audio {

/// 后端统一的输出口径。改这里要同时改两个后端的硬件配置与测试里的样例 WAV。
inline constexpr std::uint32_t kOutputSampleRate = 48000U;
inline constexpr std::uint16_t kOutputChannels = 2U;
inline constexpr std::uint16_t kOutputBitsPerSample = 16U;

/// 一次 read_stereo 内部最多从文件里搬多少帧（栈上 1 KB / 512 帧 × 2 声道 × 2 字节）。
inline constexpr std::size_t kChunkFrames = 512U;

/// WAV 头里本类真正用到的字段。
struct WavInfo {
  std::uint32_t sample_rate = 0;
  std::uint16_t channels = 0;
  std::uint16_t bits_per_sample = 0;
  std::uint32_t data_offset = 0;  ///< data 块首个采样的文件偏移
  std::uint32_t data_bytes = 0;   ///< data 块字节数

  /// 一帧 = 所有声道各一个采样。
  [[nodiscard]] std::uint32_t frame_bytes() const noexcept {
    return static_cast<std::uint32_t>(channels) * static_cast<std::uint32_t>(bits_per_sample / 8U);
  }

  [[nodiscard]] std::uint32_t total_frames() const noexcept {
    const std::uint32_t bytes = frame_bytes();
    return (bytes == 0U) ? 0U : (data_bytes / bytes);
  }

  [[nodiscard]] std::uint32_t duration_ms() const noexcept {
    return (sample_rate == 0U)
               ? 0U
               : static_cast<std::uint32_t>((static_cast<std::uint64_t>(total_frames()) * 1000U) /
                                            sample_rate);
  }
};

/**
 * 从已打开的 WAV 文件读头，找到 data 块。只看不挪指针之外的事情：
 * 返回时文件指针停在 data_offset。失败：头不对 / 不是 RIFF/WAVE → corrupt_data；
 * 不是 PCM 16 bit / 48 kHz / 单双声道 → unsupported；读不动 → io_failure。
 */
[[nodiscard]] Error parse_wav_header(std::FILE* file, WavInfo& out) noexcept;

/**
 * 把 percent（0..100）加到一块交织 S16 采样上（原地）。100% 直通，0% 全零。
 * 单独暴露是为了让测试能直接钉住「每 6 dB 掉 1 bit」这条语义。
 */
void apply_gain(std::int16_t* samples, std::size_t count, std::uint8_t percent) noexcept;

/**
 * 一个文件的播放状态机。**不是线程安全的**：调用方（两个后端）自己保证同一时刻只有一个
 * 线程碰它 —— 宿主用自旋锁护住 SDL 回调与主线程，真机只有一个喂数据的任务 + 主线程写，
 * 也走同一把锁。
 */
class WavPlayer {
 public:
  WavPlayer() noexcept = default;
  ~WavPlayer();

  WavPlayer(const WavPlayer&) = delete;
  WavPlayer& operator=(const WavPlayer&) = delete;

  /// 打开文件并定位到 data 块。成功时 state 仍是 idle（要显式 play）。重复调用会先关旧的。
  [[nodiscard]] Error open(const char* path) noexcept;
  void close() noexcept;

  [[nodiscard]] Error play() noexcept;
  [[nodiscard]] Error pause() noexcept;
  [[nodiscard]] Error resume() noexcept;
  [[nodiscard]] Error stop() noexcept;
  [[nodiscard]] Error seek(std::uint32_t position_ms) noexcept;
  [[nodiscard]] Error set_volume(std::uint8_t percent) noexcept;

  [[nodiscard]] hal::AudioState state() const noexcept { return state_; }
  [[nodiscard]] std::uint32_t position_ms() const noexcept;
  [[nodiscard]] std::uint32_t duration_ms() const noexcept { return info_.duration_ms(); }
  [[nodiscard]] std::uint8_t volume() const noexcept { return volume_percent_; }
  [[nodiscard]] const WavInfo& info() const noexcept { return info_; }
  [[nodiscard]] bool opened() const noexcept { return file_ != nullptr; }

  /**
   * 取最多 frames 个输出帧（48 kHz / 双声道 / 交织 S16）写进 out，返回真正写了几帧。
   * 非 playing 时返回 0（后端据此输出静音，位置不动）。文件到底了返回的帧数会**少于**
   * 请求值，此时 state 自动回 idle、位置停在末尾。
   */
  std::size_t read_stereo(std::int16_t* out, std::size_t frames) noexcept;

 private:
  std::FILE* file_ = nullptr;
  WavInfo info_{};
  hal::AudioState state_ = hal::AudioState::idle;
  std::uint8_t volume_percent_ = 100U;
  std::uint32_t position_frames_ = 0;
  std::int16_t mono_scratch_[kChunkFrames] = {};  ///< 单声道源复制到双声道用的中转
};

}  // namespace embark::audio

#endif /* EMBARK_PLATFORM_COMMON_WAV_PLAYER_H */
