/**
 * 音频服务单测（issue 09）：WAV 头解析 + 播放状态机 + 音量衰减
 *
 * 这一层**不需要声卡**：被测对象是 core/common/wav_player.cpp（宿主与真机共用的
 * 那一份），它只碰 stdio。所以状态机能在 CI 里跑，而不是「插上喇叭听一下」。
 *
 * 样例 WAV 是**测试自己写出来的**（不是仓库里的二进制资源）：长度、采样率、声道数
 * 都随用例现算 —— 仓库里那份 chime.wav 是给 --audio-test 听响的，不是给断言用的。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <doctest/doctest.h>

#include <embark/error.h>
#include <embark/hal/audio.h>

#include "wav_player.h"

namespace {

constexpr std::uint32_t kRate = embark::audio::kOutputSampleRate;  // 48000

void put_u16(std::uint8_t* p, std::uint16_t value) noexcept {
  p[0] = static_cast<std::uint8_t>(value & 0xFFU);
  p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::uint8_t* p, std::uint32_t value) noexcept {
  p[0] = static_cast<std::uint8_t>(value & 0xFFU);
  p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  p[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  p[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

/// 写一个 44 字节头 + PCM 数据的 WAV。数据全是同一个值，方便断言增益。
bool write_wav(const char* path, std::uint32_t rate, std::uint16_t channels, std::uint16_t bits,
               std::uint32_t frames, std::int16_t sample) noexcept {
  const std::uint64_t data_bytes = static_cast<std::uint64_t>(frames) * channels * (bits / 8U);
  if (data_bytes > 0xFFFFU) {
    return false;
  }
  std::uint8_t header[44] = {};
  std::memcpy(header, "RIFF", 4);
  put_u32(header + 4, static_cast<std::uint32_t>(36U + data_bytes));
  std::memcpy(header + 8, "WAVE", 4);
  std::memcpy(header + 12, "fmt ", 4);
  put_u32(header + 16, 16U);
  put_u16(header + 20, 1U);  // PCM
  put_u16(header + 22, channels);
  put_u32(header + 24, rate);
  put_u32(header + 28, rate * channels * (bits / 8U));
  put_u16(header + 32, static_cast<std::uint16_t>(channels * (bits / 8U)));
  put_u16(header + 34, bits);
  std::memcpy(header + 36, "data", 4);
  put_u32(header + 40, static_cast<std::uint32_t>(data_bytes));

  std::FILE* file = std::fopen(path, "wb");
  if (file == nullptr) {
    return false;
  }
  bool ok = std::fwrite(header, 1, sizeof header, file) == sizeof header;
  for (std::uint64_t i = 0; ok && i < static_cast<std::uint64_t>(frames) * channels; ++i) {
    std::uint8_t raw[2];
    put_u16(raw, static_cast<std::uint16_t>(sample));
    ok = std::fwrite(raw, 1, 2, file) == 2;
  }
  std::fclose(file);
  return ok;
}

/// 每个用例用自己的文件名，免得并行跑起来互相踩。
struct TempWav {
  const char* path;
  TempWav(const char* name, std::uint16_t channels, std::uint32_t frames, std::int16_t sample,
          std::uint32_t rate = kRate, std::uint16_t bits = 16)
      : path(name) {
    REQUIRE(write_wav(path, rate, channels, bits, frames, sample));
  }
  ~TempWav() { std::remove(path); }
};

}  // namespace

TEST_CASE("WAV 头解析：48 kHz 单声道 10 ms 的文件读出正确时长") {
  TempWav wav("embark_test_audio_mono.wav", 1U, 480U, 1000);

  embark::audio::WavPlayer player;
  REQUIRE(player.open(wav.path) == embark::Error::none);
  CHECK(player.info().channels == 1U);
  CHECK(player.info().sample_rate == kRate);
  CHECK(player.info().total_frames() == 480U);
  CHECK(player.duration_ms() == 10U);
  CHECK(player.state() == embark::hal::AudioState::idle);  // open 不自动播
  CHECK(player.position_ms() == 0U);
}

TEST_CASE("WAV 头解析：不认的格式给出的是 unsupported，坏文件是 corrupt_data") {
  TempWav wrong_rate("embark_test_audio_44k.wav", 1U, 480U, 0, 44100U);
  TempWav wrong_bits("embark_test_audio_8bit.wav", 1U, 480U, 0, kRate, 8U);

  embark::audio::WavPlayer player;
  CHECK(player.open(wrong_rate.path) == embark::Error::unsupported);
  CHECK_FALSE(player.opened());
  CHECK(player.open(wrong_bits.path) == embark::Error::unsupported);
  CHECK(player.open("embark_test_audio_missing.wav") == embark::Error::not_found);

  // 不是 RIFF：随便一段文本
  const char* junk = "embark_test_audio_junk.wav";
  std::FILE* file = std::fopen(junk, "wb");
  REQUIRE(file != nullptr);
  std::fputs("this is not a wav file at all", file);
  std::fclose(file);
  CHECK(player.open(junk) == embark::Error::corrupt_data);
  std::remove(junk);
}

TEST_CASE("状态机：play → 位置前进 → pause 停住 → resume 续 → stop 归零") {
  TempWav wav("embark_test_audio_state.wav", 1U, kRate / 10U, 0);  // 100 ms

  embark::audio::WavPlayer player;
  CHECK(player.play() == embark::Error::not_ready);  // 没开文件
  REQUIRE(player.open(wav.path) == embark::Error::none);
  CHECK(player.duration_ms() == 100U);
  CHECK(player.play() == embark::Error::none);
  CHECK(player.state() == embark::hal::AudioState::playing);

  std::int16_t frames[64 * embark::audio::kOutputChannels] = {};
  // 48000 Hz 下 64 帧 ≈ 1.33 ms —— 位置按帧记账，这里只要求它真的前进了。
  const std::size_t got = player.read_stereo(frames, 64U);
  CHECK(got == 64U);
  CHECK(player.position_ms() == 1U);

  CHECK(player.pause() == embark::Error::none);
  CHECK(player.state() == embark::hal::AudioState::paused);
  const std::uint32_t paused_at = player.position_ms();
  CHECK(player.read_stereo(frames, 64U) == 0U);  // 暂停时不产出，也不推进位置
  CHECK(player.position_ms() == paused_at);

  CHECK(player.resume() == embark::Error::none);
  CHECK(player.state() == embark::hal::AudioState::playing);
  CHECK(player.read_stereo(frames, 64U) == 64U);
  CHECK(player.position_ms() > paused_at);

  CHECK(player.stop() == embark::Error::none);
  CHECK(player.state() == embark::hal::AudioState::idle);
  CHECK(player.position_ms() == 0U);
}

TEST_CASE("状态机：seek 夹在 [0, duration]，播到底自动回 idle 且位置停在末尾") {
  TempWav wav("embark_test_audio_seek.wav", 1U, kRate / 100U, 0);  // 10 ms

  embark::audio::WavPlayer player;
  REQUIRE(player.open(wav.path) == embark::Error::none);
  CHECK(player.seek(5U) == embark::Error::none);
  CHECK(player.position_ms() == 5U);
  CHECK(player.seek(9999U) == embark::Error::none);  // 超过末尾：夹到末尾
  CHECK(player.position_ms() == player.duration_ms());

  CHECK(player.play() == embark::Error::none);  // 已在末尾：从头再来
  CHECK(player.position_ms() == 0U);

  std::int16_t frames[480 * embark::audio::kOutputChannels] = {};
  const std::size_t got = player.read_stereo(frames, 480U);
  CHECK(got == 480U);  // 10 ms = 480 帧
  CHECK(player.state() == embark::hal::AudioState::idle);
  CHECK(player.position_ms() == 10U);
}

TEST_CASE("单声道源复制到两个声道，音量按百分比衰减") {
  TempWav wav("embark_test_audio_gain.wav", 1U, 4U, 20000);

  embark::audio::WavPlayer player;
  REQUIRE(player.open(wav.path) == embark::Error::none);
  CHECK(player.set_volume(50U) == embark::Error::none);
  CHECK(player.play() == embark::Error::none);

  std::int16_t frames[4 * embark::audio::kOutputChannels] = {};
  REQUIRE(player.read_stereo(frames, 4U) == 4U);
  for (std::size_t i = 0; i < 4U; ++i) {
    CHECK(frames[(i * embark::audio::kOutputChannels) + 0U] == 10000);  // 20000 × 50%
    CHECK(frames[(i * embark::audio::kOutputChannels) + 1U] ==
          10000);  // 左右一致（复制，不是混响）
  }
}

TEST_CASE("音量边界：0..100 之外拒绝且不改当前值，0 是静音") {
  TempWav wav("embark_test_audio_volume.wav", 1U, 2U, 1000);

  embark::audio::WavPlayer player;
  REQUIRE(player.open(wav.path) == embark::Error::none);
  CHECK(player.volume() == 100U);
  CHECK(player.set_volume(101U) == embark::Error::invalid_argument);
  CHECK(player.volume() == 100U);

  CHECK(player.set_volume(0U) == embark::Error::none);
  CHECK(player.play() == embark::Error::none);
  std::int16_t frames[2 * embark::audio::kOutputChannels] = {};
  REQUIRE(player.read_stereo(frames, 2U) == 2U);
  for (const std::int16_t sample : frames) {
    CHECK(sample == 0);  // 静音 = 采样全零，不是关掉功放（见 audio.h 的硬约束 ①）
  }
}

TEST_CASE("apply_gain：100% 直通，负值按比例收，count 为 0 不碰指针") {
  std::int16_t samples[4] = {-1000, -500, 500, 1000};
  embark::audio::apply_gain(samples, 4U, 100U);
  CHECK(samples[0] == -1000);
  CHECK(samples[3] == 1000);

  embark::audio::apply_gain(samples, 4U, 50U);
  CHECK(samples[0] == -500);
  CHECK(samples[1] == -250);
  CHECK(samples[2] == 250);
  CHECK(samples[3] == 500);

  embark::audio::apply_gain(nullptr, 4U, 50U);  // 不解引用空指针
  embark::audio::apply_gain(samples, 0U, 10U);  // 不写内存
  CHECK(samples[0] == -500);
}
