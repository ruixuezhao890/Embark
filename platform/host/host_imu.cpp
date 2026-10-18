/**
 * 宿主 · 姿态服务实现（issue 08）
 *
 * 两条曲线都是「时间 → 值」的纯函数，没有随机数、没有累积误差：同一时刻跑两次读到的值不同，
 * 但**同一个时间点**永远读到同一个值。验收脚本要的正是这个（可复现），真机噪声不在宿主模拟。
 */
#include "host_imu.h"

#include <cmath>

namespace embark::platform::host {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double shake_period_s = 2.0;     // 摇动周期（issue 08 指定 2 s）
constexpr double shake_amplitude_g = 0.5;  // 加速度幅度 0.5 g
constexpr double spin_period_s = 1.0;  // 角速度周期（比摇动快一倍：手感上像“来回掰”）
constexpr double spin_amplitude_dps = 30.0;  // 角速度幅度 30 °/s
constexpr float resting_temp_c = 25.0F;
constexpr float gravity_z_g = -1.0F;  // 屏幕朝上平放：重力沿 -Z

}  // namespace

Error HostImu::init() noexcept {
  if (ready_) {
    return Error::none;  // 幂等
  }
  start_ = std::chrono::steady_clock::now();
  ready_ = true;
  return Error::none;
}

Error HostImu::read(hal::ImuSample& out) noexcept {
  if (!ready_) {
    return Error::not_ready;  // 没 init 就给数，等于把“驱动没起来”伪装成“静止”
  }

  out.accel_x = 0.0F;
  out.accel_y = 0.0F;
  out.accel_z = gravity_z_g;
  out.gyro_x = 0.0F;
  out.gyro_y = 0.0F;
  out.gyro_z = 0.0F;
  out.temperature_c = resting_temp_c;

  if (shake_) {
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    const double accel_phase = 2.0 * pi * seconds / shake_period_s;
    const double spin_phase = 2.0 * pi * seconds / spin_period_s;
    out.accel_x = static_cast<float>(shake_amplitude_g * std::sin(accel_phase));
    out.accel_y = static_cast<float>(shake_amplitude_g * std::sin(accel_phase + pi / 2.0));
    out.gyro_x = static_cast<float>(spin_amplitude_dps * std::sin(spin_phase));
    out.gyro_z = static_cast<float>(spin_amplitude_dps * std::cos(spin_phase));
  }
  return Error::none;
}

}  // namespace embark::platform::host
