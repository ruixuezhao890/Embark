/**
 * Embark ESP32-S3 · 音频输出（I2S → 板载 DAC → 功放 → 喇叭）
 *
 * 硬件：板载 PCM5101 DAC + 功放，接线 **BCK = IO48 / WS(LRCK) = IO38 / DOUT = IO47**（微雪
 * wiki 引脚表；esp32_board.h 的 audio 一节是唯一事实来源）。芯片没有 MCLK 脚，用不上。
 *
 * ## 音量只能是"软件衰减"
 *
 * PCM5101 的规格页写死 `Control interface: HW` —— 它没有 I2C/SPI 寄存器，板上那个音量旋钮
 * 接的是**功放的模拟增益**，MCU 看不到也拧不动。所以 hal::IAudio::set_volume 在这里实现成
 * 采样乘系数：每降 6 dB 掉 1 bit 有效精度（-40 dB 时 16 bit 只剩 10 bit 左右），0% 是
 * 采样全零（静音，不是断电）。这条与 include/embark/hal/audio.h 的说明一致。
 *
 * ## 与宿主共用同一套解析/流控
 *
 * 读头、定位 data 块、按帧推进、算位置、音量 —— 全在 platform/common/wav_player.cpp
 * （宿主那侧用同一个类喂 SDL）。这里只负责「把 PCM 塞进 I2S DMA」，以及一个后台任务。
 *
 * ## 任务
 *
 * 一个落核 0 的后台任务（与 UI 任务错核）：读一块 → 写一块。写阻塞在 i2s_channel_write 上，
 * 满了就等 DMA 腾位置，不会忙等。没在播的时候按 10 ms 粒度空转（不打 I2S）。
 */
#ifndef EMBARK_PLATFORM_ESP32_AUDIO_H
#define EMBARK_PLATFORM_ESP32_AUDIO_H

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <embark/error.h>
#include <embark/hal/audio.h>

#include "wav_player.h"

namespace embark::platform::esp32 {

class Esp32Audio final : public hal::IAudio {
 public:
  Esp32Audio() noexcept = default;
  Esp32Audio(const Esp32Audio&) = delete;
  Esp32Audio& operator=(const Esp32Audio&) = delete;
  ~Esp32Audio() override;

  /// 建 I2S 通道（std 模式 / master / 48 kHz / 16 bit / 立体声）→ 使能 → 起后台任务。
  /// 幂等。失败返回 io_failure 或 unsupported；调用方（Esp32Hal::init）**不**当致命错误：
  /// 没有音频的板子照样要能跑（喇叭没插也一样）。
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

  /// I2S 通道建起来且使能了。不代表喇叭在响（那要看 state()）。
  [[nodiscard]] bool channel_ready() const noexcept { return channel_ready_; }

 private:
  static void task_entry(void* arg) noexcept;
  void pump() noexcept;
  void lock() noexcept;
  void unlock() noexcept;

  /// 每次喂给 I2S 的帧数：512 帧 @48 kHz ≈ 10.7 ms —— 与宿主 SDL 那侧的缓冲同量级。
  static constexpr std::size_t kChunkFrames = 512U;
  static constexpr std::size_t kChunkSamples = kChunkFrames * audio::kOutputChannels;

  audio::WavPlayer player_;
  /// 保护 player_ 的互斥量（任务与 UI 线程两边都会碰它）。
  /// 不用 std::atomic_flag：xtensa 上 `__atomic_test_and_set` 不在默认库里（真机链接
  /// 报 undefined reference），而 FreeRTOS 的互斥量本来就是这块板子的标准做法。
  SemaphoreHandle_t lock_ = nullptr;
  /// i2s_chan_handle_t，用 void* 存：IDF 的驱动头不进本头文件（esp32_hal.h 会包含它）。
  void* tx_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool channel_ready_ = false;
  std::int16_t buffer_[kChunkSamples] = {};
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_AUDIO_H */
