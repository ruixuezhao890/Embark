/**
 * Embark 宿主入口
 *
 * 今天做三件事：初始化 HAL（issues/04）→ 打启动信息 → 跑一遍 HAL 自检（时间 / 持久化 /
 * 总线 / 系统）。真正的宿主运行时（FreeRTOS 任务 + SDL 显示/输入 + LVGL + 单一 UI 任务）
 * 按 issues/05、06 补进来。
 *
 * stdout 设成无缓冲：程序若被强杀，块缓冲会让"最后几行"全丢掉（issue 02 踩过）。
 * 输出规矩：文本格式化一律用 efmt，文本输出一律用 elog。
 */
#include <cstdio>
#include <cstdint>

#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark/version.h>
#include <middleware/elog/elog.hpp>

#include "host_context.h"

namespace {

void put_u32_le(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::uint32_t get_u32_le(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8U) |
         (static_cast<std::uint32_t>(in[2]) << 16U) | (static_cast<std::uint32_t>(in[3]) << 24U);
}

/// 把 HAL 的每条能力各走一遍。失败不是异常路径 —— 是"这条分支在宿主机上真的跑过"的证据。
int self_check(embark::hal::Context& ctx) {
  int failures = 0;

  const std::uint32_t before = ctx.time.now_ms();
  ctx.time.delay_ms(5);
  const std::uint32_t after = ctx.time.now_ms();
  if (after < before) {
    ELOG_ERROR("自检失败：now_ms 倒退（{} → {}）", before, after);
    ++failures;
  }
  const auto epoch = ctx.time.epoch_ms();
  if (!epoch.has_value()) {
    ELOG_ERROR("自检失败：epoch_ms 返回 {}", embark::to_string(epoch.error()));
    ++failures;
  } else {
    ELOG_INFO("时间：now_ms={} 毫秒，epoch_ms 可用", after);
  }

  // 持久化：boot_count 每次运行 +1，能累加就证明这文件真的落盘了。
  bool storage_ok = true;
  std::uint8_t raw[4] = {};
  std::uint32_t boot_count = 0;
  const auto read = ctx.storage.read("boot_count", etl::span<std::uint8_t>(raw, sizeof(raw)));
  if (read.has_value()) {
    if (read.value() != sizeof(raw)) {
      ELOG_ERROR("自检失败：boot_count 长度是 {}，应为 4", read.value());
      storage_ok = false;
    } else {
      boot_count = get_u32_le(raw);
    }
  } else if (read.error() != embark::Error::not_found) {
    ELOG_ERROR("自检失败：读 boot_count 返回 {}", embark::to_string(read.error()));
    storage_ok = false;
  }

  const std::uint32_t next_boot_count = boot_count + 1;
  put_u32_le(raw, next_boot_count);
  if (ctx.storage.write("boot_count", etl::span<const std::uint8_t>(raw, sizeof(raw))) !=
      embark::Error::none) {
    ELOG_ERROR("自检失败：写 boot_count 失败");
    storage_ok = false;
  }

  // 擦除路径也要走一遍：写一条 → 擦掉 → 确认 not_found。
  const std::uint8_t scratch[1] = {0x5A};
  if (ctx.storage.write("self_check", etl::span<const std::uint8_t>(scratch, 1)) !=
      embark::Error::none) {
    ELOG_ERROR("自检失败：写 self_check 失败");
    storage_ok = false;
  } else if (ctx.storage.erase("self_check") != embark::Error::none) {
    ELOG_ERROR("自检失败：erase self_check 失败");
    storage_ok = false;
  } else {
    std::uint8_t sink[1] = {};
    const auto gone =
            ctx.storage.read("self_check", etl::span<std::uint8_t>(sink, sizeof(sink)));
    if (!gone.has_value() && gone.error() != embark::Error::not_found) {
      ELOG_ERROR("自检失败：擦除后读回的不是 not_found");
      storage_ok = false;
    }
  }

  if (storage_ok) {
    ELOG_INFO("持久化：boot_count={}（本次），capacity={} 字节", next_boot_count,
              ctx.storage.capacity_bytes());
  } else {
    ++failures;
  }

  // 宿主没有物理总线：unsupported 就是正确结果（不是失败）。
  const std::uint8_t probe[1] = {0x00};
  const embark::Error bus = ctx.bus.i2c_write(0x3C, etl::span<const std::uint8_t>(probe, 1));
  if (bus != embark::Error::unsupported) {
    ELOG_ERROR("自检失败：宿主 i2c_write 返回 {}，应为 unsupported", embark::to_string(bus));
    ++failures;
  } else {
    ELOG_INFO("总线：宿主无物理总线，i2c/spi 一律 unsupported");
  }

  ELOG_INFO("系统：free_heap={} 字节（0 = 该后端量不了），看门狗由平台自己喂",
            ctx.system.free_heap_bytes());

  return failures;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  auto& platform = embark::platform::host::HostHal::instance();
  const embark::Error init = platform.init();
  if (init != embark::Error::none) {
    std::fprintf(stderr, "[embark] HAL 初始化失败：%s\n", embark::to_string(init));
    return 1;
  }

  auto& ctx = platform.context();
  ELOG_INFO("Embark {}（平台 {}）宿主启动，持久化文件 {}", embark::version_string(),
            embark::platform_name(), platform.storage_path());

  const int failures = self_check(ctx);
  if (failures != 0) {
    ELOG_ERROR("HAL 自检未通过：{} 项失败", failures);
    return 1;
  }

  ELOG_INFO("HAL 自检通过（时间 / 持久化 / 总线 / 系统）");
  return 0;
}
