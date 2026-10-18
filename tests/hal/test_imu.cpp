// 姿态服务（ADR 0019 / issue 08）：IImu 的三条契约。
//
// 不碰硬件、不碰 LVGL：这一层要钉住的是「没起来时别给假数据」这件事 ——
// 一个把 not_ready 当静止的上层，比一个读不到数据的上层危险得多。
#include <doctest/doctest.h>

#include <embark/app_registry.h>
#include <embark/framework.h>

#include "fakes/fake_imu.h"
#include "fakes/fake_ui_port.h"
#include "fakes/fakes.h"

namespace {

/// 读一次姿态然后记住结果的 App：验「Context 的 imu 槽真的能用」而不是只验类型。
class ProbeImuApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "probe-imu"; }

  void onCreate(embark::Framework& fw) override {
    has_imu = fw.hal().imu != nullptr;
    if (!has_imu) {
      return;
    }
    embark::hal::ImuSample sample;
    read_ok = (fw.hal().imu->read(sample) == embark::Error::none);
    accel_z = sample.accel_z;
  }
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onExit() override {}

  bool has_imu = false;
  bool read_ok = false;
  float accel_z = 0.0F;
};

}  // namespace

TEST_CASE("IImu：没 init 就 read 返回 not_ready，且不写脏数据") {
  embark::fakes::FakeImu imu;
  embark::hal::ImuSample sample;
  sample.accel_z = 42.0F;  // 哨兵值：read 失败时不许被覆盖成 0

  CHECK(imu.read(sample) == embark::Error::not_ready);
  CHECK(sample.accel_z == doctest::Approx(42.0F));
}

TEST_CASE("IImu：init 幂等 —— 连调两次都成功，之后 read 可用") {
  embark::fakes::FakeImu imu;
  CHECK(imu.init() == embark::Error::none);
  CHECK(imu.init() == embark::Error::none);
  CHECK(imu.ready());

  embark::hal::ImuSample sample;
  CHECK(imu.read(sample) == embark::Error::none);
}

TEST_CASE("IImu：read 把后端采到的物理量原样交出来") {
  embark::fakes::FakeImu imu;
  embark::hal::ImuSample given;
  given.accel_x = 0.25F;
  given.accel_y = -0.5F;
  given.accel_z = -1.0F;  // 屏幕朝上平放：合矢量 ≈ 1 g
  given.gyro_z = 12.5F;
  given.temperature_c = 25.0F;
  imu.set_sample(given);
  REQUIRE(imu.init() == embark::Error::none);

  embark::hal::ImuSample sample;
  REQUIRE(imu.read(sample) == embark::Error::none);
  CHECK(sample.accel_x == doctest::Approx(0.25F));
  CHECK(sample.accel_y == doctest::Approx(-0.5F));
  CHECK(sample.accel_z == doctest::Approx(-1.0F));
  CHECK(sample.gyro_z == doctest::Approx(12.5F));
  CHECK(sample.temperature_c == doctest::Approx(25.0F));
}

TEST_CASE("IImu：总线读失败原样往上抛（不退回旧值、不当静止）") {
  embark::fakes::FakeImu imu;
  imu.read_result = embark::Error::io_failure;
  REQUIRE(imu.init() == embark::Error::none);

  embark::hal::ImuSample sample;
  sample.accel_z = 42.0F;
  CHECK(imu.read(sample) == embark::Error::io_failure);
  CHECK(sample.accel_z == doctest::Approx(42.0F));  // 失败就不许写 out
}

TEST_CASE("IImu：这块板没有芯片时 init 返回 not_found，之后 read 是 not_ready") {
  // 真机上这条路径很常见：芯片没焊 / SA0 接法不同 / 上拉缺失。它必须是「能降级运行」，
  // 不是「启动失败」——Esp32Hal::init 因此只记日志不返回错误。
  embark::fakes::FakeImu imu;
  imu.init_result = embark::Error::not_found;
  CHECK(imu.init() == embark::Error::not_found);
  CHECK_FALSE(imu.ready());

  embark::hal::ImuSample sample;
  CHECK(imu.read(sample) == embark::Error::not_ready);
}

TEST_CASE("Context：imu 槽接上服务后，App 在 onCreate 里读得到真值") {
  embark::fakes::FakeHal hal;
  embark::fakes::FakeImu imu;
  embark::hal::ImuSample given;
  given.accel_z = -1.0F;
  imu.set_sample(given);
  REQUIRE(imu.init() == embark::Error::none);

  auto ctx = hal.context();
  ctx.imu = &imu;  // 三个装配点之外，这里是最小的一份：只接 IMU，audio/network 留空
  embark::fakes::FakeUiPort ui;
  embark::Framework fw(ctx, embark::app_registry<ProbeImuApp>(), &ui);

  REQUIRE(fw.boot() == embark::Error::none);

  const auto* app = static_cast<const ProbeImuApp*>(fw.apps().at(0));
  CHECK(app->has_imu);
  CHECK(app->read_ok);
  CHECK(app->accel_z == doctest::Approx(-1.0F));
  CHECK(imu.read_calls == 1);

  fw.shutdown();
}
