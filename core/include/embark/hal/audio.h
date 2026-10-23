/**
 * HAL · 音频服务（ADR 0019 的 L1 设备 + L2 流，issue 09）
 *
 * 一句话：**把卡上一个文件播出来，并且能暂停/继续/跳转/调音量**。播放列表、下一首、
 * 音量记忆、进度条怎么画，全归 App（ADR 0019 决策 3：业务服务不进基建）。
 *
 * ## 范围：本层只认 WAV
 *
 * WAV（PCM）= 零解码器、零额外依赖，读头 → 直接把采样喂给 I2S/SDL。
 * MP3 / AAC 要引解码器（minimp3 约 0.5 MB 代码体积，libmad/helix 更重），那是一次独立的
 * 依赖决策，另开一票。本接口的 `play(const char* path)` 因此**不承诺任意识别格式的能力**：
 * 打不开或不是 PCM WAV 就返回 `unsupported` / `corrupt_data`，不做格式嗅探猜测。
 *
 * ## 硬约束 ①：音量是软件衰减，不是硬件音量
 *
 * 板上的音频芯片（型号见 device/src/esp32_board.h）**没有**数字音量寄存器 ——
 * 厂商规格页写死 "Control interface: HW"，没有 I2C/SPI 控制口。板上的音量旋钮接的是
 * 功放级的模拟增益，**MCU 看不到它**。所以 `set_volume()` 唯一能做的事是在 PCM 采样上
 * 乘系数：
 *   - 每衰减 6 dB 掉 1 bit 有效精度（50% ≈ -6 dB ⇒ 16 bit 变 15 bit 有效）；
 *   - `0` 只能做到「采样全零」，做不到「关掉功放」—— 底噪仍在。
 * 要真正断电静音得等 App 层去拉功放的使能脚（那是板级 GPIO，不在本接口里）。
 *
 * ## 硬约束 ②：不要用 IDisplay::set_backlight() 表达音量
 *
 * 板上有两个「亮度」：屏幕背光与喇叭音量。它们在后端是两套完全不同的东西（LEDC 占空比
 * vs PCM 采样系数），历史上 `IDisplay::set_backlight()` 把「亮度」语义硬编码进了显示
 * 接口 —— 本接口出现之后这两件事在接口上是分开的，**别想着复用**。
 *
 * ## 为什么 position/duration 返回毫秒而不是帧
 *
 * 帧数是采样率的函数（48 kHz 下 1 帧 = 20.8 µs），毫秒是物理时间。上层画进度条、
 * 判「播完了没」都按时间思考；换算落在实现里。
 */
#ifndef EMBARK_HAL_AUDIO_H
#define EMBARK_HAL_AUDIO_H

#include <cstdint>

#include <embark/error.h>

namespace embark::hal {

/// 播放状态。idle = 没在播（初始 / stop 之后 / 播到文件末尾）。
enum class AudioState : std::uint8_t { idle, playing, paused };

class IAudio {
 public:
  virtual ~IAudio() = default;

  /**
   * 打开输出设备（宿主 = SDL 音频设备，真机 = I2S 通道 + 喂数据的任务）。幂等。
   *
   * 失败语义：unsupported = 这个平台没有音频输出（宿主没声卡 / 后端没编进来）；
   * io_failure = 设备开了但配不起来。调用方**不该**把这里的失败当致命错误 ——
   * 没喇叭的板子照样要能跑，接口在这种情况下所有 play() 都返回 not_ready。
   */
  virtual Error init() noexcept = 0;

  /**
   * 开始播一个 WAV 文件。**阻塞到流打开为止**（解析头 + 建好流），返回后由后端自己推进
   * —— 上层不需要（也没有）tick 来决定什么时候喂数据。
   *
   * 失败语义：not_ready = init() 没成过；not_found = 文件打不开；unsupported = 不是
   * 本后端认的格式（比如不是 PCM 16 bit）；corrupt_data = 头不对。
   * 正在播时再调 play 会先停掉当前那个，再开新的。
   */
  virtual Error play(const char* path) noexcept = 0;

  /// 暂停：位置保持不动。非 playing 时调用返回 none（幂等，不报错）。
  virtual Error pause() noexcept = 0;

  /// 继续：从暂停处接着播。非 paused 时返回 none。
  virtual Error resume() noexcept = 0;

  /// 停：回 idle，位置归零。之后再 play 从头来。
  virtual Error stop() noexcept = 0;

  /// 跳到 position_ms（会被夹到 [0, duration_ms]）。播到末尾之后再 seek 回中间不会自动续播。
  virtual Error seek(std::uint32_t position_ms) noexcept = 0;

  [[nodiscard]] virtual AudioState state() const noexcept = 0;

  /// 当前位置（毫秒）。idle 且没打开文件时是 0。
  [[nodiscard]] virtual std::uint32_t position_ms() const noexcept = 0;

  /// 当前文件总长（毫秒）。没打开文件时是 0。
  [[nodiscard]] virtual std::uint32_t duration_ms() const noexcept = 0;

  /**
   * 设音量 0..100（百分比，见上面硬约束 ①：这是软件采样衰减）。
   * 越界值返回 invalid_argument 且不改当前音量。默认 100。
   */
  virtual Error set_volume(std::uint8_t percent) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_AUDIO_H */
