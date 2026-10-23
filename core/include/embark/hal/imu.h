/**
 * HAL · 姿态服务（ADR 0019 的 L2 设备服务，issue 08）
 *
 * 一句话：**读一次六轴加速与角速度**。这就是 App 想要的姿态数据的全部形状 ——
 * 「摇晃」的判据、「抬腕亮屏」的阈值、「屏幕跟着转」的轴向换算，都是 App 的事，不进这一层
 * （ADR 0019 决策 3：业务服务不进基建）。
 *
 * ## 为什么单位是 g 和 dps，不是原始 LSB
 *
 * 原始 LSB 是**量程的函数**：同一颗芯片设成 ±2 g 与 ±16 g，同一个加速度值读出来差 8 倍。
 * 如果接口给 LSB，换一颗芯片（或只是改一次量程）上层每一处判断都要跟着改 —— 那就等于
 * 接口被某颗芯片的细节绑死了（ADR 0002 的原始判据）。g 与 dps 是物理量，换芯片接口不变；
 * **换算系数落在实现里**（按实现自己选的量程算），上层永远拿到 -1.0 = 一个重力。
 *
 * ## 坐标系
 *
 * 三轴命名与轴向由**实现**定义并写在它的头注释里（宿主与真机可以不同）——接口只承诺
 * 「三轴正交、符合右手系、静止时合矢量模长 ≈ 1 g」。这条不写死是因为板子上芯片怎么贴
 * 是板级事实，芯片型号名与旋转矩阵都不许出现在本文件（AGENTS.md 硬规则）。
 *
 * ## 为什么没有「就绪回调」
 *
 * 数据速率（ODR）由实现固定，上层按需读：读一次 I2C 12 字节在 400 kHz 下 < 1 ms，
 * App 在自己的 onForegroundTick 里直接读就行，不需要中断、不需要 FIFO、不需要后台任务。
 * 要更高频率就提高 ODR 并在 App 里连续读 —— 那是 App 的取舍，不是接口要预留的能力。
 */
#ifndef EMBARK_HAL_IMU_H
#define EMBARK_HAL_IMU_H

#include <embark/error.h>

namespace embark::hal {

/** 一次姿态采样。所有字段都带默认值，实现可以只填它有的那几个。 */
struct ImuSample {
  float accel_x = 0.0F;  ///< 加速度 g（1.0 = 一个重力）。静止平放时合矢量模长 ≈ 1.0
  float accel_y = 0.0F;
  float accel_z = 0.0F;
  float gyro_x = 0.0F;  ///< 角速度 dps（度/秒）。静止 ≈ 0
  float gyro_y = 0.0F;
  float gyro_z = 0.0F;
  float temperature_c = 0.0F;  ///< 芯片结温 °C（不是环境温度，会偏高几度）
};

class IImu {
 public:
  virtual ~IImu() = default;

  /**
   * 找芯片、配量程与数据率并开传感器。幂等：已经就绪时直接返回 none。
   *
   * 失败语义：not_found = 这条总线上没有应答的芯片（没焊 / 地址不对 / 引脚接反）；
   * io_failure = 有应答但配置写不进去；unsupported = 这个平台没有姿态服务。
   * 调用方**不该**把这里的失败当致命错误 —— 没有 IMU 的板子照样要能跑。
   */
  virtual Error init() noexcept = 0;

  /**
   * 同步读一次。未 init / init 失败后调用返回 not_ready（不是崩、不是脏数据）。
   * 单位见 ImuSample。总线上读失败返回 io_failure —— 偶发 NACK 不该被当成 0 值传播出去。
   */
  virtual Error read(ImuSample& out) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_IMU_H */
