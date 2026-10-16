/**
 * 平台共用层 · 运行时字库服务（ADR 0007 / issue 17）
 *
 * 把 LVGL 的 'F' 盘接到 hal::IFileSystem 上，并用 FontLedger 记账：
 *
 *   load("ui_font_x.bin")  →  "F:/ui_font_x.bin"  →  lv_fs_open  →  lv_font_load
 *
 * 同一路径重复 load 只把引用 +1（返回同一个 lv_font_t*），release 归零时才 lv_font_free。
 *
 * 为什么不用 LVGL 自带的 LV_USE_FS_POSIX / LV_USE_FS_STDIO：它们的根是编译期宏
 * （LV_FS_POSIX_PATH），而宿主（仓库目录）与真机（将来的 SD 挂载点）不同、开发期还要能
 * 临时换根 —— "根"属于平台后端，所以走 hal::IFileSystem（ADR 0002 的能力粒度）。
 *
 * 一个进程只能有一个活动实例：LVGL 的驱动表一个盘符只能挂一个驱动、且注册只增不减，
 * 所以回调与驱动描述都是文件级静态量，本类只负责"装配 + 调用"。真机侧还没有文件系统
 * 后端（Context::filesystem == nullptr），init() 会返回 not_ready —— 真机 v1 用的是静态
 * 子集字库（assets/fonts/embark_zh_14.c），运行时加载留给 SD 后端那个 issue。
 */
#ifndef EMBARK_PLATFORM_COMMON_FONT_SERVICE_H
#define EMBARK_PLATFORM_COMMON_FONT_SERVICE_H

#include <cstddef>

#include <lvgl.h>

#include <embark/error.h>
#include <embark/font_ledger.h>
#include <embark/hal/filesystem.h>
#include <embark_limits.h>
#include <middleware/etl/expected.h>
#include <middleware/etl/string_view.h>

namespace embark::platform {

class FontService {
 public:
  /// 两个引用都要活过本对象（宿主是进程级静态对象，见 platform/host/host_context.h）。
  FontService(hal::IFileSystem& filesystem, FontLedger& ledger) noexcept
      : filesystem_(&filesystem), ledger_(&ledger) {}

  FontService(const FontService&) = delete;
  FontService& operator=(const FontService&) = delete;

  /// 注册 LVGL 的 'F' 盘并把文件系统接上（幂等：驱动注册只做一次）。
  /// 必须在 lv_init() 之后调用（framework boot 里的 LvglUiPort::init 就是那个点）。
  /// 文件系统未就绪 → not_ready。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] bool ready() const noexcept { return mounted_; }

  /// 加载字库：path 是**相对文件系统根**的路径（不含盘符，例如 "ui_font_ui_heiti_14.bin"）。
  /// 已在账上的路径只 +1 引用并返回同一个指针；新路径先查文件大小记账，再 lv_font_load。
  /// 错误：not_ready（没 init）｜invalid_argument（路径空/超长）｜not_found｜no_space（记账）｜
  /// io_failure（读文件）｜corrupt_data（文件在、但不是能加载的字库）。
  [[nodiscard]] etl::expected<lv_font_t*, Error> load(etl::string_view path) noexcept;

  /// 释放一笔引用；归零时才 lv_font_free。账上没有这个路径 → not_found。
  [[nodiscard]] Error release(etl::string_view path) noexcept;

  [[nodiscard]] const FontLedger& ledger() const noexcept { return *ledger_; }

 private:
  struct Loaded {
    char path[font_path_bytes] = {};
    lv_font_t* font = nullptr;
  };

  static constexpr std::size_t kNoSlot = font_max_loaded;

  [[nodiscard]] std::size_t find_loaded(etl::string_view path) const noexcept;
  void drop_loaded(std::size_t slot) noexcept;

  hal::IFileSystem* filesystem_ = nullptr;
  FontLedger* ledger_ = nullptr;
  bool mounted_ = false;
  Loaded loaded_[font_max_loaded]{};
};

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_COMMON_FONT_SERVICE_H */
