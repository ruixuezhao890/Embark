#include "esp32_audio.h"

#include <cstring>

#include "driver/i2s_std.h"
#include "esp32_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <middleware/elog/elog.hpp>

namespace embark::platform::esp32 {
namespace {

constexpr const char* kLogTag = "audio";

/// 后台任务的栈（字节）。这条路只有「读一块 + 写一块 + 一次 memset」，4 KB 够。
constexpr std::uint32_t kTaskStackBytes = 4096U;
constexpr UBaseType_t kTaskPriority = 5U;
/// 没在播时的空转粒度：10 ms 一次「看看有没有活干」，比一帧（约 30 ms）细，响应够快又不空烧 CPU。
constexpr TickType_t kIdleTicks = pdMS_TO_TICKS(10);

}  // namespace

Esp32Audio::~Esp32Audio() {
  if (task_ != nullptr) {
    vTaskDelete(task_);
    task_ = nullptr;
  }
  if (lock_ != nullptr) {
    vSemaphoreDelete(lock_);
    lock_ = nullptr;
  }
  lock();
  player_.close();
  unlock();
  if (tx_ != nullptr) {
    auto handle = static_cast<i2s_chan_handle_t>(tx_);
    i2s_channel_disable(handle);
    i2s_del_channel(handle);
    tx_ = nullptr;
    channel_ready_ = false;
  }
}

void Esp32Audio::lock() noexcept {
  if (lock_ != nullptr) {
    xSemaphoreTake(lock_, portMAX_DELAY);
  }
}

void Esp32Audio::unlock() noexcept {
  if (lock_ != nullptr) {
    xSemaphoreGive(lock_);
  }
}

Error Esp32Audio::init() noexcept {
  if (channel_ready_) {
    return Error::none;
  }
  if (audio_pin_bclk < 0 || audio_pin_lrck < 0 || audio_pin_dout < 0) {
    ELOG_WARN("音频：板级引脚没填（bclk/lrck/dout = {}/{}/{}）—— 音频输出关闭", audio_pin_bclk,
              audio_pin_lrck, audio_pin_dout);
    return Error::unsupported;
  }

  // 互斥量要在后台任务起来之前建好（任务一启动就会去锁 player_）。
  if (lock_ == nullptr) {
    lock_ = xSemaphoreCreateMutex();
    if (lock_ == nullptr) {
      ELOG_WARN("音频：互斥量创建失败 —— 音频输出关闭");
      return Error::io_failure;
    }
  }

  // std 模式 / 主模式：BCK 与 WS 都由我们自己产生，DAC 从模式（PCM5101 就是这样接的）。
  i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_config.auto_clear = true;  // 写空时输出 0，而不是上一轮的残留

  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chan_config, &tx, nullptr) != ESP_OK) {
    ELOG_WARN("音频：i2s_new_channel 失败 —— 音频输出关闭");
    return Error::io_failure;
  }

  i2s_std_config_t std_config = {};
  std_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(audio::kOutputSampleRate);
  std_config.slot_cfg =
      I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std_config.gpio_cfg.mclk = I2S_GPIO_UNUSED;  // PCM5101 没有 MCLK 脚
  std_config.gpio_cfg.bclk = static_cast<gpio_num_t>(audio_pin_bclk);
  std_config.gpio_cfg.ws = static_cast<gpio_num_t>(audio_pin_lrck);
  std_config.gpio_cfg.dout = static_cast<gpio_num_t>(audio_pin_dout);
  std_config.gpio_cfg.din = I2S_GPIO_UNUSED;  // 只出不进

  const esp_err_t configured = i2s_channel_init_std_mode(tx, &std_config);
  if (configured != ESP_OK) {
    ELOG_WARN("音频：i2s_channel_init_std_mode 失败（{}）—— 音频输出关闭",
              static_cast<int>(configured));
    i2s_del_channel(tx);
    return Error::io_failure;
  }
  if (i2s_channel_enable(tx) != ESP_OK) {
    ELOG_WARN("音频：i2s_channel_enable 失败 —— 音频输出关闭");
    i2s_del_channel(tx);
    return Error::io_failure;
  }

  tx_ = tx;
  channel_ready_ = true;

  // 后台任务落核 0（UI 任务在核 1）：音频不会跟界面抢同一个核。
  if (xTaskCreatePinnedToCore(&Esp32Audio::task_entry, "embark_audio", kTaskStackBytes, this,
                              kTaskPriority, &task_, own_task_core) != pdPASS) {
    ELOG_WARN("音频：后台任务起不来 —— 音频输出关闭");
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    tx_ = nullptr;
    channel_ready_ = false;
    return Error::io_failure;
  }

  ELOG_INFO("音频：I2S 就绪（{} Hz / 16 bit / 立体声，BCK={} WS={} DOUT={}）",
            static_cast<int>(audio::kOutputSampleRate), audio_pin_bclk, audio_pin_lrck,
            audio_pin_dout);
  return Error::none;
}

void Esp32Audio::task_entry(void* arg) noexcept {
  auto* self = static_cast<Esp32Audio*>(arg);
  if (self == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  for (;;) {
    self->pump();
  }
}

void Esp32Audio::pump() noexcept {
  if (tx_ == nullptr) {
    vTaskDelay(kIdleTicks);
    return;
  }

  // 先看一眼要不要干活（state/位置都在锁里读，别和主线程抢）。
  lock();
  const bool playing = player_.state() == hal::AudioState::playing;
  unlock();
  if (!playing) {
    vTaskDelay(kIdleTicks);
    return;
  }

  lock();
  const std::size_t produced = player_.read_stereo(buffer_, kChunkFrames);
  unlock();

  if (produced == 0U) {
    // 刚播完：让出一次，下一次进 state 就是 idle 了。
    vTaskDelay(kIdleTicks);
    return;
  }
  if (produced < kChunkFrames) {
    // 尾部补静音：DMA 要的是一整块，不补会把上一块的声音又放一遍。
    std::memset(buffer_ + (produced * audio::kOutputChannels), 0,
                (kChunkFrames - produced) * audio::kOutputChannels * sizeof(buffer_[0]));
  }

  std::size_t written = 0;
  // 长度按字节：帧数 × 每帧样本数 × 每样本字节数。超时给足（等 DMA 腾位置）。
  const std::size_t bytes = kChunkSamples * sizeof(buffer_[0]);
  const esp_err_t result = i2s_channel_write(static_cast<i2s_chan_handle_t>(tx_), buffer_, bytes,
                                             &written, pdMS_TO_TICKS(1000));
  if (result != ESP_OK) {
    // 写不进去一般不是"没声音"而是 DMA 卡住：记一次日志然后让出，别刷屏。
    static std::uint32_t failures = 0;
    if ((failures++ % 100U) == 0U) {
      ELOG_WARN("音频：i2s_channel_write 失败（{}）", static_cast<int>(result));
    }
    vTaskDelay(kIdleTicks);
  }
}

Error Esp32Audio::play(const char* path) noexcept {
  if (!channel_ready_) {
    return Error::not_ready;
  }
  lock();
  Error opened = player_.open(path);
  if (opened == Error::none) {
    opened = player_.play();
  }
  unlock();
  if (opened != Error::none) {
    ELOG_WARN("音频：播放 {} 失败（{}）", path, opened);
    return opened;
  }
  ELOG_INFO("音频：开始播放 {}（{} ms）", path, duration_ms());
  return Error::none;
}

Error Esp32Audio::pause() noexcept {
  lock();
  const Error result = player_.pause();
  unlock();
  return result;
}

Error Esp32Audio::resume() noexcept {
  lock();
  const Error result = player_.resume();
  unlock();
  return result;
}

Error Esp32Audio::stop() noexcept {
  lock();
  const Error result = player_.stop();
  unlock();
  return result;
}

Error Esp32Audio::seek(std::uint32_t position_ms) noexcept {
  lock();
  const Error result = player_.seek(position_ms);
  unlock();
  return result;
}

hal::AudioState Esp32Audio::state() const noexcept {
  auto* self = const_cast<Esp32Audio*>(this);
  self->lock();
  const hal::AudioState state = player_.state();
  self->unlock();
  return state;
}

std::uint32_t Esp32Audio::position_ms() const noexcept {
  auto* self = const_cast<Esp32Audio*>(this);
  self->lock();
  const std::uint32_t position = player_.position_ms();
  self->unlock();
  return position;
}

std::uint32_t Esp32Audio::duration_ms() const noexcept {
  auto* self = const_cast<Esp32Audio*>(this);
  self->lock();
  const std::uint32_t duration = player_.duration_ms();
  self->unlock();
  return duration;
}

Error Esp32Audio::set_volume(std::uint8_t percent) noexcept {
  lock();
  const Error result = player_.set_volume(percent);
  unlock();
  return result;
}

}  // namespace embark::platform::esp32
