# 09 · 音频服务 IAudio（L1 设备 + L2 流）

Status: done（2026-10-18）
Blocked by: 07

## 为什么

Q7（m00145）用户点名"还有音频，没实现"。Q11 拍板 L1+L2 进基建、L3（播放列表/下一首/音量记忆）归用户。

**这一项比 IMU 贵得多，原因不是驱动难，是能力面根本不存在**：

- `hal::IBus` 只有三个方法：`i2c_write` / `i2c_write_read` / `spi_transfer`（`include/embark/hal/bus.h`）
- I2S 是第三类总线语义（连续流 + DMA + 时钟主从），既不是 I2C 也不是 SPI ⇒ **必须新增接口，不能扩 IBus**
- `include/` 27 个头里没有任何音频概念

## 接口形状

`include/embark/hal/audio.h`

`enum class AudioState : std::uint8_t { idle, playing, paused };`

`class IAudio {`
` public:`
`  virtual Error init() noexcept = 0;`
`  virtual Error play(const char* path) noexcept = 0;   // 阻塞到流打开为止，之后由后端自己推进`
`  virtual Error pause() noexcept = 0;`
`  virtual Error resume() noexcept = 0;`
`  virtual Error stop() noexcept = 0;`
`  virtual Error seek(std::uint32_t position_ms) noexcept = 0;`
`  virtual AudioState state() const noexcept = 0;`
`  virtual std::uint32_t position_ms() const noexcept = 0;`
`  virtual std::uint32_t duration_ms() const noexcept = 0;`
`  virtual Error set_volume(std::uint8_t percent) noexcept = 0;   // 0..100`
`};`

## 三条必须写进注释的硬约束

**① 音量是软件衰减，不是硬件音量**

PCM5101 的规格页写死 `Control interface: HW` —— 没有 I2C/SPI 寄存器、没有数字音量。板上的"音量旋钮"接的是功放模拟增益，**ESP32 看不到**。所以 `set_volume()` 只能在 PCM 采样上乘系数：每衰减 6 dB 掉 1 bit 有效精度，`0` 只能做到静音而不是断电。

**② I2S 引脚**（`platform/esp32/src/esp32_board.h` 里还没有，要加）

LRCK = GPIO38 / DIN = GPIO47 / BCK = GPIO48。48 kHz、16 bit、双声道（单声道源复制到两声道）。

**③ `IDisplay::set_backlight()` 把"亮度"语义硬编码进了显示接口**

板上两个亮度：屏幕背光 + 喇叭音量。`set_volume()` 出现后这两件事在接口上是分开的，**不要试图复用 `set_backlight`**。`IDisplay` 本轮不动。

## 范围切分

**本票只做 WAV**（PCM 直通，零解码器）。MP3/AAC 要引解码器（minimp3 是单头文件约 0.5 MB 代码体积，libmad/helix 更重），那是一个独立的依赖决策，**另开一票**，不塞进这里。

## 宿主怎么落

`platform/host/host_audio.{h,cpp}`：SDL2 音频设备 + 一个读 WAV 头的小解析器。SDL2 已经在（`embark_find_sdl2`），`embark_host_ui` 已经链着。

宿主与真机**共用同一套 WAV 解析与流控**（放 `platform/common/`），只有"把 PCM 喂给谁"不同（宿主 = SDL2 回调，真机 = I2S DMA）。

## 验收

- 宿主：`embark_host_ui --ui --delay 40 --audio-test` 播放 `app/aidogui_ui/assets/audio/*.wav`，日志打时长/进度/状态迁移
- 真机：插喇叭播放，`set_volume(100)` 与 `set_volume(30)` 听得出差别
- `tests/hal/test_audio.cpp`：fake 后端验状态机（`play` 前 `position_ms()==0`；`pause` 后位置不动；`stop` 后回 `idle`）
- 不回归四条既有验收命令

## 收尾记录（2026-10-18）

### 落了什么

- **接口**：`include/embark/hal/audio.h`（99 行，其中 34 行头注释）——`AudioState{idle,playing,paused}`、
  `IAudio`（`init` / `play(path)` / `pause` / `resume` / `stop` / `seek(ms)` / `state` / `position_ms` /
  `duration_ms` / `set_volume(0..100)`）。头注释写全了三条硬约束（音量是软件衰减、只认 PCM WAV、
  别拿 `set_backlight` 表达音量）与「为什么返回毫秒不是帧」。
- **共用层**：`platform/common/wav_player.{h,cpp}`（285 行 .cpp）—— WAV 头解析 + 流控状态机（宿主与真机共用），
  后端只负责「把 PCM 喂给谁」。
- **宿主**：`platform/host/host_audio.{h,cpp}`（172 行 .cpp，SDL2 音频设备 + 回调），`HostContext` 装配 + `init()`。
- **真机**：`platform/esp32/src/esp32_audio.{h,cpp}`（263 行 .cpp）—— `I2S_NUM_0` STD/Philips/48 kHz/16 bit 立体声、
  `auto_clear = true`、`mclk = I2S_GPIO_UNUSED`，512 帧一块的后台任务落**核 0**（UI 落核 1）。
- **板级**：`platform/esp32/src/esp32_board.h` 新增「板载音频」段（`audio_pin_bclk = 48`、
  `audio_pin_lrck = 38`、`audio_pin_dout = 47`，填 -1 即关音频）。
- **测试**：`tests/hal/test_audio.cpp`（221 行；假后端就写在这个文件里，没进 `tests/fakes/`，因为只有它用到）。
- **资产**：`app/aidogui_ui/assets/audio/chime.wav`（115,244 B）。

### 三个实测撞出来的坑（都已写进 `docs/reference/pitfalls.md`）

1. **xtensa 上 `std::atomic_flag` 链接失败**：`undefined reference to '__atomic_test_and_set'`
   （默认库没有那个 libatomic 符号；宿主的 MinGW 有，所以只在真机构建暴露）。真机侧改用 FreeRTOS
   互斥量（`xSemaphoreCreateMutex` / `xSemaphoreTake(lock, portMAX_DELAY)`），宿主侧保留 atomic_flag。
2. **别拿固定 `sleep` 当音频同步点**：宿主 SDL 回调极稀疏（300 ms 里只来 1 次 21 ms），首版
   「sleep 300 ms 后断言位置前进」直接失败。改成轮询 `wait_until_advanced()`（deadline 3 s / 步长 10 ms）。
3. **`sizeof std::int16_t` 在 GCC 15（MinGW）报 `expected primary-expression before ',' token`** ——
   改成 `sizeof(out[0])` 这种取元素的写法。

### 验收

- `embark_host_ui --ui --delay 40 --audio-test` → exit 0，`〔音频验收通过〕`：播放推进 21 ms → 暂停 300 ms
  位置仍 21 → 继续到 42 → `seek(600)` 偏差 0 → `stop()` 回 idle/0 → 音量边界（30 ok / 101 拒 / 100 ok）。
  该开关在建 LVGL **之前**就验完退出（音频不需要屏幕）。
- `tests/hal/test_audio.cpp` 全过；`embark_tests` 累计 **139 cases / 1471 assertions**。
- 真机：`idf.py build` exit 0，`embark_esp32.bin` = 0xeea40 字节（app 分区 0x300000，剩 69%）。
  **喇叭出声与音量对比仍未实测**（手上没插喇叭跑过）—— 这一条留待上机，代码路径已就绪。
- 不回归：五条既有验收故事（`--ui` / `--click` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages`）
  与 `embark_host_tour`（17/17）全部 exit 0；`ctest` 2/2。

### 文档同步

`docs/reference/hooks-and-api.md`（Context 表 audio 行）、`docs/guides/hal-backend-guide.md`（装配说明）、
`GLOSSARY.md`（HAL Context 落地进度）、`docs/adr/0019-...md`（横幅补 09 已落地）、
`platform/esp32/README.md`（新增第 12 条「板载音频 PCM5101」+ bring-up 清单里音频指向它）、
`docs/guides/tools.md`（验收开关表补 `--audio-test` / `--imu-log` / `--imu-shake`）、
`docs/reference/pitfalls.md`（新增「音频 / I²S」三条）。

### 有意没做

- **MP3/AAC**：要引解码器（约 0.5 MB 代码体积），是独立依赖决策 —— `play()` 对它们返回 `unsupported`，
  不猜格式。
- **功放使能脚**：真正的断电静音要拉板级 GPIO，属于 App 层的事（不在 `IAudio` 里）。
- **音量记忆 / 播放列表 / 下一首**：ADR 0019 决策 3 明确归用户。
