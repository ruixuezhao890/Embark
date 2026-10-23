#include "wav_player.h"

#include <cstring>

namespace embark::audio {
namespace {

constexpr std::uint16_t kFormatPcm = 1U;

[[nodiscard]] std::uint16_t read_u16(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                    (static_cast<std::uint16_t>(p[1]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
         (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

/// 从当前位置读 n 字节，正好读到才返回 true（短读一律当失败：头被截断）。
[[nodiscard]] bool read_exact(std::FILE* file, void* buffer, std::size_t bytes) noexcept {
  return std::fread(buffer, 1, bytes, file) == bytes;
}

}  // namespace

void apply_gain(std::int16_t* samples, std::size_t count, std::uint8_t percent) noexcept {
  if (samples == nullptr || percent >= 100U) {
    return;
  }
  for (std::size_t i = 0; i < count; ++i) {
    const std::int32_t scaled = (static_cast<std::int32_t>(samples[i]) * percent) / 100;
    samples[i] = static_cast<std::int16_t>(scaled);
  }
}

Error parse_wav_header(std::FILE* file, WavInfo& out) noexcept {
  out = WavInfo{};
  if (file == nullptr) {
    return Error::invalid_argument;
  }
  if (std::fseek(file, 0, SEEK_SET) != 0) {
    return Error::io_failure;
  }

  std::uint8_t head[12] = {};
  if (!read_exact(file, head, sizeof head)) {
    return Error::corrupt_data;
  }
  const bool is_riff =
      (std::memcmp(head, "RIFF", 4) == 0) && (std::memcmp(head + 8, "WAVE", 4) == 0);
  if (!is_riff) {
    return Error::corrupt_data;
  }

  bool have_fmt = false;
  bool have_data = false;
  while (!have_data) {
    std::uint8_t chunk[8] = {};
    if (!read_exact(file, chunk, sizeof chunk)) {
      // 走到文件尾还没见到 data 块：fmt 齐了就是只有头的空文件，否则是真坏。
      return have_fmt ? Error::corrupt_data : Error::corrupt_data;
    }
    const std::uint32_t size = read_u32(chunk + 4);
    const long here = std::ftell(file);
    if (here < 0) {
      return Error::io_failure;
    }

    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      if (size < 16U) {
        return Error::corrupt_data;
      }
      std::uint8_t fmt[16] = {};
      if (!read_exact(file, fmt, sizeof fmt)) {
        return Error::corrupt_data;
      }
      const std::uint16_t format = read_u16(fmt);
      out.channels = read_u16(fmt + 2);
      out.sample_rate = read_u32(fmt + 4);
      out.bits_per_sample = read_u16(fmt + 14);
      if (format != kFormatPcm) {
        return Error::unsupported;  // IEEE float / ADPCM / 压缩格式：本后端不认
      }
      have_fmt = true;
      // 跳过 fmt 块里我们不看的尾巴（cbSize 等）。
      if (std::fseek(file, here + static_cast<long>(size), SEEK_SET) != 0) {
        return Error::io_failure;
      }
      continue;
    }

    if (std::memcmp(chunk, "data", 4) == 0) {
      if (!have_fmt) {
        return Error::corrupt_data;  // data 在 fmt 之前：不是我们能认的 WAV
      }
      out.data_offset = static_cast<std::uint32_t>(here);
      out.data_bytes = size;
      have_data = true;
      break;
    }

    // 其它块（LIST / fact / cue …）：按 RIFF 规矩跳过（长度奇数时补 1 字节）。
    const long next = here + static_cast<long>(size) + static_cast<long>(size % 2U);
    if (std::fseek(file, next, SEEK_SET) != 0) {
      return Error::io_failure;
    }
  }

  if (out.channels != 1U && out.channels != kOutputChannels) {
    return Error::unsupported;
  }
  if (out.bits_per_sample != kOutputBitsPerSample) {
    return Error::unsupported;
  }
  if (out.sample_rate != kOutputSampleRate) {
    return Error::unsupported;  // 重采样是另一件事，见头注释
  }
  if (std::fseek(file, static_cast<long>(out.data_offset), SEEK_SET) != 0) {
    return Error::io_failure;
  }
  return Error::none;
}

WavPlayer::~WavPlayer() {
  close();
}

void WavPlayer::close() noexcept {
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  info_ = WavInfo{};
  state_ = hal::AudioState::idle;
  position_frames_ = 0;
}

Error WavPlayer::open(const char* path) noexcept {
  if (path == nullptr || path[0] == '\0') {
    return Error::invalid_argument;
  }
  close();
  file_ = std::fopen(path, "rb");
  if (file_ == nullptr) {
    return Error::not_found;
  }
  const Error parsed = parse_wav_header(file_, info_);
  if (parsed != Error::none) {
    close();
    return parsed;
  }
  return Error::none;
}

Error WavPlayer::play() noexcept {
  if (file_ == nullptr) {
    return Error::not_ready;
  }
  if (state_ == hal::AudioState::playing) {
    return Error::none;
  }
  // 从暂停处继续（resume 语义）；文件已播到底则先从 0 开始，
  // 免得「播完了再点播放」什么也不发生。
  if (position_frames_ >= info_.total_frames()) {
    if (std::fseek(file_, static_cast<long>(info_.data_offset), SEEK_SET) != 0) {
      return Error::io_failure;
    }
    position_frames_ = 0;
  }
  state_ = hal::AudioState::playing;
  return Error::none;
}

Error WavPlayer::pause() noexcept {
  if (state_ == hal::AudioState::playing) {
    state_ = hal::AudioState::paused;
  }
  return Error::none;
}

Error WavPlayer::resume() noexcept {
  if (state_ == hal::AudioState::paused) {
    state_ = hal::AudioState::playing;
  }
  return Error::none;
}

Error WavPlayer::stop() noexcept {
  if (file_ != nullptr && std::fseek(file_, static_cast<long>(info_.data_offset), SEEK_SET) != 0) {
    return Error::io_failure;
  }
  state_ = hal::AudioState::idle;
  position_frames_ = 0;
  return Error::none;
}

Error WavPlayer::seek(std::uint32_t position_ms) noexcept {
  if (file_ == nullptr) {
    return Error::not_ready;
  }
  const std::uint32_t total = info_.total_frames();
  std::uint64_t frame = (static_cast<std::uint64_t>(position_ms) * info_.sample_rate) / 1000U;
  if (frame > total) {
    frame = total;
  }
  const long offset =
      static_cast<long>(info_.data_offset) + static_cast<long>(frame * info_.frame_bytes());
  if (std::fseek(file_, offset, SEEK_SET) != 0) {
    return Error::io_failure;
  }
  position_frames_ = static_cast<std::uint32_t>(frame);
  return Error::none;
}

Error WavPlayer::set_volume(std::uint8_t percent) noexcept {
  if (percent > 100U) {
    return Error::invalid_argument;
  }
  volume_percent_ = percent;
  return Error::none;
}

std::uint32_t WavPlayer::position_ms() const noexcept {
  if (info_.sample_rate == 0U) {
    return 0U;
  }
  return static_cast<std::uint32_t>((static_cast<std::uint64_t>(position_frames_) * 1000U) /
                                    info_.sample_rate);
}

std::size_t WavPlayer::read_stereo(std::int16_t* out, std::size_t frames) noexcept {
  if (out == nullptr || frames == 0U || state_ != hal::AudioState::playing || file_ == nullptr) {
    return 0;
  }

  const std::uint32_t total = info_.total_frames();
  if (position_frames_ >= total) {
    state_ = hal::AudioState::idle;
    return 0;
  }

  const std::size_t wanted =
      (static_cast<std::uint64_t>(frames) > static_cast<std::uint64_t>(total - position_frames_))
          ? static_cast<std::size_t>(total - position_frames_)
          : frames;

  std::size_t produced = 0;
  while (produced < wanted) {
    const std::size_t chunk =
        ((wanted - produced) > kChunkFrames) ? kChunkFrames : (wanted - produced);
    std::size_t got = 0;
    if (info_.channels == kOutputChannels) {
      // sizeof(out[0]) 而不是 sizeof std::int16_t：类型名走 sizeof 时在本工具链上
      // 会被当成表达式（MinGW GCC 15 的实测行为），元素类型本来就在手边，绕开它。
      got = std::fread(out + (produced * kOutputChannels), sizeof(out[0]), chunk * kOutputChannels,
                       file_) /
            kOutputChannels;
      if (got > 0U) {
        apply_gain(out + (produced * kOutputChannels), got * kOutputChannels, volume_percent_);
      }
    } else {
      got = std::fread(mono_scratch_, sizeof(mono_scratch_[0]), chunk, file_);
      for (std::size_t i = 0; i < got; ++i) {
        std::int16_t sample = mono_scratch_[i];
        apply_gain(&sample, 1U, volume_percent_);
        out[((produced + i) * kOutputChannels) + 0U] = sample;
        out[((produced + i) * kOutputChannels) + 1U] = sample;
      }
    }
    produced += got;
    if (got < chunk) {
      break;  // 文件提前到底（和被头里的 data_bytes 不一致）
    }
  }

  position_frames_ += static_cast<std::uint32_t>(produced);
  if (position_frames_ >= total) {
    position_frames_ = total;
    state_ = hal::AudioState::idle;
  }
  return produced;
}

}  // namespace embark::audio
