/**
 * 宿主 · 姿态服务（issue 08）
 *
 * 不依赖 SDL：宿主没有真的 IMU，这一份是**可复现的合成输入**。两种状态：
 *
 *   静止（默认）：(0, 0, -1) g，角速度全 0 —— 模拟「板子平放在桌上、屏幕朝上、Z 轴朝上」，
 *                于是 az = -1.0 这条断言在宿主上也是真的（与真机同号：见下面的轴向说明）。
 *   摇动（--imu-shake）：X/Y 上各一条周期 2 s、幅度 0.5 g 的正弦，Z 保持 -1.0；
 *                角速度给一条周期 1 s、幅度 30 dps 的正弦（有转动才有角速度，静止摇动不该有）。
 *
 * 轴向（本实现的定义，接口只要求右手系）：+X 向右、+Y 向上、+Z 指向屏幕外（朝用户）。
 * 屏幕朝上平放时重力指 -Z ⇒ az = -1.0。真机侧按芯片贴合方向换算，符号可能与这里相反 ——
 * 所以验收只要求「az ≈ ±1.0」（见 .scratch/app-platform/issues/08-imu.md）。
 *
 * 温度恒定 25.0 °C：宿主要的是**确定性**，不是物理真实。要测「温度显示」，25.0 比任何
 * 带噪声的值都好用。
 */
#ifndef EMBARK_PLATFORM_HOST_IMU_H
#define EMBARK_PLATFORM_HOST_IMU_H

#include <chrono>

#include <embark/error.h>
#include <embark/hal/imu.h>

namespace embark::platform::host {

class HostImu final : public hal::IImu {
 public:
  HostImu() noexcept = default;
  HostImu(const HostImu&) = delete;
  HostImu& operator=(const HostImu&) = delete;
  ~HostImu() override = default;

  /// 幂等。第一次调用记下时间原点（摇动的相位从这里算），之后只返回 none。
  [[nodiscard]] Error init() noexcept override;

  [[nodiscard]] Error read(hal::ImuSample& out) noexcept override;

  /// --imu-shake 开关：打开后 read() 按正弦输出（周期 2 s、幅度 0.5 g）。
  void set_shake(bool shake) noexcept { shake_ = shake; }

  [[nodiscard]] bool ready() const noexcept { return ready_; }

 private:
  bool ready_ = false;
  bool shake_ = false;
  std::chrono::steady_clock::time_point start_{};
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_IMU_H */
