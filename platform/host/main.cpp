/**
 * Embark 宿主入口（骨架期）
 *
 * 现在只做三件事：建日志、打版本、把 ETL 定容容器与 efmt 格式化各用一次 ——
 * 目的是让"依赖链路是否通"变成一个可以跑出来的事实，而不是一份文档承诺。
 *
 * 真正的宿主运行时（FreeRTOS 任务 + SDL 显示/输入 + LVGL + 单一 UI 任务）按
 * issues/02、05、06 补。
 *
 * 输出规矩（沿用 efmt-elog 仓库的约定）：文本格式化一律用 efmt，文本输出一律用 elog。
 */
#include <elog/elog.hpp>
#include <embark/version.h>
#include <middleware/etl/vector.h>

namespace {

constexpr const char* kLoggerName = "embark";

}  // namespace

int main() {
  e_log::logger* const logger =
          e_log::create_logger(kLoggerName, e_log::stdout_sink(), e_log::level::debug);
  if (logger == nullptr) {
    return 1;  // 日志建不起来，后面就没有出口了
  }

  ELOG_INFO("Embark {}（平台 {}）宿主骨架启动", embark::version_string(), embark::platform_name());

  // 依赖链路自检：ETL 定容容器（编译期容量、零堆）+ efmt 格式化
  etl::vector<int, 4> numbers;
  numbers.push_back(2);
  numbers.push_back(3);
  ELOG_INFO("ETL 定容容器：size={} capacity={}", numbers.size(), numbers.capacity());

  ELOG_INFO("middleware 视图 / elog / efmt 链路正常");
  return 0;
}
