/**
 * 运行时字库服务实现：见 font_service.h 头注释。
 *
 * 驱动回调的口径来自 LVGL 8.3 的 lv_fs 约定（third_party/lvgl/src/misc/lv_fs.h）：
 *   * open_cb 返回的 void* 就是后续回调收到的 file_p（LVGL 存进 lv_fs_file_t::file_d），
 *     所以这里返回槽位地址（不是句柄数字：句柄 0 是无效值，塞进指针里没法自证）；
 *   * read_cb 的 br 可能是 NULL（lv_font_loader.c:203 就读一个字节、不关心读了多少），
 *     所以 br 只在非空时才写；
 *   * lv_font_loader 只用 LV_FS_SEEK_SET，但 seek/tell 还是按三种 whence 给全了 ——
 *     别的 LVGL 使用者（图片解码、缓存）会用到 CUR/END。
 */
#include "font_service.h"

#include <cstdio>
#include <cstring>

namespace embark::platform {
namespace {

/// 生成代码与字库文件都走 'F' 盘（'S' 盘已经给了 AiDoGUI 资源，别撞）。
constexpr char kDriveLetter = 'F';

/// 一个打开中的文件：句柄 + 我们自己维护的读写位置（hal::IFileSystem 只有绝对 seek）。
struct OpenFile {
  hal::FileHandle handle = hal::invalid_file_handle;
  std::size_t position = 0;
  std::size_t bytes = 0;
};

OpenFile g_open[filesystem_max_open_files]{};
hal::IFileSystem* g_filesystem = nullptr;
lv_fs_drv_t g_drv{};  // LVGL 只存指针，必须活到进程结束
bool g_mounted = false;

bool ready_cb(lv_fs_drv_t* drv) {
  (void)drv;
  return g_mounted && g_filesystem != nullptr;
}

void* open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
  (void)drv;
  if (!g_mounted || g_filesystem == nullptr || path == nullptr) {
    return nullptr;
  }
  if ((mode & LV_FS_MODE_WR) != 0) {
    return nullptr;  // 只读盘：运行时字库没有"写回"这回事
  }
  const char* relative = path;
  while (*relative == '/' || *relative == '\\') {
    ++relative;  // LVGL 剥掉盘符后还带前导 '/'（lv_fs_get_real_path）
  }

  const auto opened = g_filesystem->open(etl::string_view(relative));
  if (!opened.has_value()) {
    return nullptr;
  }
  const auto bytes = g_filesystem->size(*opened);
  if (!bytes.has_value()) {
    g_filesystem->close(*opened);
    return nullptr;
  }

  for (OpenFile& slot : g_open) {
    if (slot.handle != hal::invalid_file_handle) {
      continue;
    }
    slot.handle = *opened;
    slot.position = 0;
    slot.bytes = *bytes;
    return &slot;
  }
  g_filesystem->close(*opened);
  return nullptr;  // 槽位用完：上一位使用者没关文件
}

lv_fs_res_t close_cb(lv_fs_drv_t* drv, void* file_p) {
  (void)drv;
  OpenFile* slot = static_cast<OpenFile*>(file_p);
  if (slot == nullptr || slot->handle == hal::invalid_file_handle) {
    return LV_FS_RES_INV_PARAM;
  }
  const Error closed = g_filesystem->close(slot->handle);
  slot->handle = hal::invalid_file_handle;
  slot->position = 0;
  slot->bytes = 0;
  return (closed == Error::none) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

lv_fs_res_t read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
  (void)drv;
  OpenFile* slot = static_cast<OpenFile*>(file_p);
  if (br != nullptr) {
    *br = 0;  // LVGL 允许不关心读了多少字节（br == NULL）
  }
  if (slot == nullptr || buf == nullptr || slot->handle == hal::invalid_file_handle) {
    return LV_FS_RES_INV_PARAM;
  }
  if (btr == 0) {
    return LV_FS_RES_OK;
  }

  const auto got = g_filesystem->read(
      slot->handle, etl::span<std::uint8_t>(static_cast<std::uint8_t*>(buf), btr));
  if (!got.has_value()) {
    return LV_FS_RES_FS_ERR;
  }
  if (br != nullptr) {
    *br = static_cast<uint32_t>(*got);
  }
  slot->position += *got;
  return LV_FS_RES_OK;
}

lv_fs_res_t seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
  (void)drv;
  OpenFile* slot = static_cast<OpenFile*>(file_p);
  if (slot == nullptr || slot->handle == hal::invalid_file_handle) {
    return LV_FS_RES_INV_PARAM;
  }

  std::size_t target = pos;
  if (whence == LV_FS_SEEK_CUR) {
    target = slot->position + pos;
  } else if (whence == LV_FS_SEEK_END) {
    target = slot->bytes + pos;  // pos = 0 即文件尾；> 0 会落到下面"越过文件尾"
  }
  if (target > slot->bytes) {
    return LV_FS_RES_INV_PARAM;
  }
  if (g_filesystem->seek(slot->handle, target) != Error::none) {
    return LV_FS_RES_FS_ERR;
  }
  slot->position = target;
  return LV_FS_RES_OK;
}

lv_fs_res_t tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
  (void)drv;
  OpenFile* slot = static_cast<OpenFile*>(file_p);
  if (slot == nullptr || pos_p == nullptr || slot->handle == hal::invalid_file_handle) {
    return LV_FS_RES_INV_PARAM;
  }
  *pos_p = static_cast<uint32_t>(slot->position);
  return LV_FS_RES_OK;
}

}  // namespace

Error FontService::init() noexcept {
  if (filesystem_ == nullptr || ledger_ == nullptr) {
    return Error::not_ready;
  }
  if (lv_fs_get_drv(kDriveLetter) == nullptr) {
    lv_fs_drv_init(&g_drv);
    g_drv.letter = kDriveLetter;
    g_drv.cache_size = 0;  // 不缓存：字库是流式读，多一层缓存只是多一份内存
    g_drv.ready_cb = ready_cb;
    g_drv.open_cb = open_cb;
    g_drv.close_cb = close_cb;
    g_drv.read_cb = read_cb;
    g_drv.seek_cb = seek_cb;
    g_drv.tell_cb = tell_cb;
    lv_fs_drv_register(&g_drv);
  }
  g_filesystem = filesystem_;
  g_mounted = true;
  mounted_ = true;
  return Error::none;
}

std::size_t FontService::find_loaded(etl::string_view path) const noexcept {
  for (std::size_t i = 0; i < font_max_loaded; ++i) {
    if (loaded_[i].font != nullptr && etl::string_view(loaded_[i].path) == path) {
      return i;
    }
  }
  return kNoSlot;
}

void FontService::drop_loaded(std::size_t slot) noexcept {
  loaded_[slot] = Loaded{};  // 表小（font_max_loaded）：不做搬移，find 跳过空条目
}

etl::expected<lv_font_t*, Error> FontService::load(etl::string_view path) noexcept {
  if (!mounted_ || filesystem_ == nullptr) {
    return etl::unexpected(Error::not_ready);
  }
  if (path.empty() || path.size() + 1 > font_path_bytes) {
    return etl::unexpected(Error::invalid_argument);
  }

  const std::size_t existing = find_loaded(path);
  if (existing != kNoSlot) {
    // 复用：同一个字库继续驻留，只把引用 +1（命中时 FontLedger::acquire 不看 bytes，
    // 只在这个路径从没记过账、真要新增条目时才用它算预算）。
    const auto refs = ledger_->acquire(path, 1);
    if (!refs.has_value()) {
      return etl::unexpected(refs.error());
    }
    return loaded_[existing].font;
  }

  // 先问文件多大（记账要按字节），再决定加载。
  const auto handle = filesystem_->open(path);
  if (!handle.has_value()) {
    return etl::unexpected(handle.error());
  }
  const auto bytes = filesystem_->size(*handle);
  filesystem_->close(*handle);
  if (!bytes.has_value()) {
    return etl::unexpected(bytes.error());
  }

  const auto refs = ledger_->acquire(path, *bytes);
  if (!refs.has_value()) {
    return etl::unexpected(refs.error());
  }

  char lv_path[font_path_bytes + 4] = {};
  std::snprintf(lv_path, sizeof(lv_path), "%c:/%s", kDriveLetter, path.data());
  lv_font_t* font = lv_font_load(lv_path);
  if (font == nullptr) {
    static_cast<void>(ledger_->release(path));  // 没加载成功就不占额度
    return etl::unexpected(Error::corrupt_data);  // 文件在、也读得动：那就是格式/内存问题
  }

  for (std::size_t i = 0; i < font_max_loaded; ++i) {
    if (loaded_[i].font == nullptr) {
      std::memcpy(loaded_[i].path, path.data(), path.size());
      loaded_[i].path[path.size()] = '\0';
      loaded_[i].font = font;
      return font;
    }
  }

  // 账上还有额度但表里没槽位：只可能是 font_max_loaded 与预算口径不一致，别静默漏掉。
  lv_font_free(font);
  static_cast<void>(ledger_->release(path));
  return etl::unexpected(Error::no_space);
}

Error FontService::release(etl::string_view path) noexcept {
  const std::size_t slot = find_loaded(path);
  if (slot == kNoSlot) {
    return Error::not_found;
  }
  const auto refs = ledger_->refs(path);
  if (!refs.has_value()) {
    return Error::not_found;  // 指针表与账目不同步：宁可报错，也别猜
  }
  const Error released = ledger_->release(path);
  if (released != Error::none) {
    return released;
  }
  if (*refs > 1) {
    return Error::none;  // 还有引用：字库继续驻留
  }
  lv_font_free(loaded_[slot].font);
  drop_loaded(slot);
  return Error::none;
}

}  // namespace embark::platform
