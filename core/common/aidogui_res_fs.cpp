/**
 * AiDoGUI 资源盘（'S'）实现：见 aidogui_res_fs.h 头注释。
 *
 * 只依赖 stdio + LVGL：宿主与真机（esp_vfs_fat 的 POSIX 面）同一份实现。
 */
#include "aidogui_res_fs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <lvgl.h>

#ifndef EMBARK_AIDOGUI_RES_ROOT
#define EMBARK_AIDOGUI_RES_ROOT ""
#endif

namespace embark::platform {
namespace {

// 生成代码里的盘符（AiDoGUI 的 C 模板写死 S:，不是工程设置）。
constexpr char kDriveLetter = 'S';

std::string g_root;           // 挂载根（空 = 未挂载）
bool g_mounted = false;       // 卸载只翻这个标志：驱动注册一次就够
lv_fs_drv_t g_drv{};          // 驱动描述（必须活到进程结束，LVGL 只存指针）
AidoguiResFsStats g_stats{};  // 验收统计

bool ready_cb(lv_fs_drv_t* drv) {
  (void)drv;
  return g_mounted;
}

// 目录名单复数兜底：生成代码写的是 "/fonts/" 与 "/images/"（AiDoGUI 的 C 模板口径），
// 而它的导出目录里实际叫 "font/" 与 "image/"。为了让「导出产物原样拷到 S: 盘根下就能跑」
// （GUI 导出选完目录就完事，不需要谁去改名），下面按对照表在打不开时再试一次。
// 只做这一处精确替换，不猜别的目录名。
constexpr struct {
  const char* from;
  const char* to;
} kDirFallbacks[] = {
    {"/fonts/", "/font/"},
    {"/images/", "/image/"},
};

// 把 path 里第一处 from 换成 to；没命中返回空串（调用方据此跳过）。
std::string replace_dir(const std::string& path, const char* from, const char* to) {
  const std::size_t pos = path.find(from);
  if (pos == std::string::npos) {
    return std::string();
  }
  std::string out = path;
  out.replace(pos, std::strlen(from), to);
  return out;
}

// LVGL 交进来的是去掉盘符的路径（lv_fs_get_real_path），例如 "/fonts/x.bin"。
std::string full_path(const char* path) {
  std::string full = g_root;
  if (path != nullptr && path[0] != '\0') {
    if (path[0] != '/' && path[0] != '\\') {
      full += '/';
    }
    full += path;
  }
  return full;
}

void* open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
  (void)drv;
  if (!g_mounted) {
    return nullptr;
  }
  const char* flags = "rb";
  if ((mode & LV_FS_MODE_WR) != 0) {
    flags = ((mode & LV_FS_MODE_RD) != 0) ? "rb+" : "wb";
  }
  const std::string full = full_path(path);
  FILE* file = std::fopen(full.c_str(), flags);
  for (const auto& fallback : kDirFallbacks) {
    if (file != nullptr) {
      break;
    }
    const std::string alt = replace_dir(full, fallback.from, fallback.to);
    if (!alt.empty()) {
      file = std::fopen(alt.c_str(), flags);
    }
  }
  if (file == nullptr) {
    ++g_stats.opens_failed;
    return nullptr;
  }
  ++g_stats.opens_ok;
  return file;
}

lv_fs_res_t close_cb(lv_fs_drv_t* drv, void* file_p) {
  (void)drv;
  if (file_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  return (std::fclose(static_cast<FILE*>(file_p)) == 0) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

lv_fs_res_t read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
  (void)drv;
  if (file_p == nullptr || buf == nullptr || br == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  auto* file = static_cast<FILE*>(file_p);
  *br = static_cast<uint32_t>(std::fread(buf, 1, btr, file));
  if (*br < btr && std::ferror(file) != 0) {
    ++g_stats.reads_failed;
    return LV_FS_RES_FS_ERR;
  }
  ++g_stats.reads_ok;
  return LV_FS_RES_OK;
}

lv_fs_res_t write_cb(lv_fs_drv_t* drv, void* file_p, const void* buf, uint32_t btw, uint32_t* bw) {
  (void)drv;
  if (file_p == nullptr || buf == nullptr || bw == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  *bw = static_cast<uint32_t>(std::fwrite(buf, 1, btw, static_cast<FILE*>(file_p)));
  return (*bw == btw) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

lv_fs_res_t seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
  (void)drv;
  if (file_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  int origin = SEEK_SET;
  if (whence == LV_FS_SEEK_CUR) {
    origin = SEEK_CUR;
  } else if (whence == LV_FS_SEEK_END) {
    origin = SEEK_END;
  }
  return (std::fseek(static_cast<FILE*>(file_p), static_cast<long>(pos), origin) == 0)
             ? LV_FS_RES_OK
             : LV_FS_RES_FS_ERR;
}

lv_fs_res_t tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
  (void)drv;
  if (file_p == nullptr || pos_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }
  const long pos = std::ftell(static_cast<FILE*>(file_p));
  if (pos < 0) {
    return LV_FS_RES_FS_ERR;
  }
  *pos_p = static_cast<uint32_t>(pos);
  return LV_FS_RES_OK;
}

void register_driver_once() {
  if (lv_fs_get_drv(kDriveLetter) != nullptr) {
    return;  // 已经注册过（卸载不清驱动，重挂不必再注册）
  }
  lv_fs_drv_init(&g_drv);
  g_drv.letter = kDriveLetter;
  g_drv.cache_size = 0;  // 不缓存：BIN 是流式读，缓存只会多占内存
  g_drv.ready_cb = ready_cb;
  g_drv.open_cb = open_cb;
  g_drv.close_cb = close_cb;
  g_drv.read_cb = read_cb;
  g_drv.write_cb = write_cb;
  g_drv.seek_cb = seek_cb;
  g_drv.tell_cb = tell_cb;
  lv_fs_drv_register(&g_drv);
}

}  // namespace

const char* aidogui_res_fs_default_root() {
  const char* from_env = std::getenv("EMBARK_AIDOGUI_RES");
  if (from_env != nullptr && from_env[0] != '\0') {
    return from_env;
  }
  return EMBARK_AIDOGUI_RES_ROOT;
}

bool aidogui_res_fs_mount(const char* root_dir) {
  const char* root =
      (root_dir != nullptr && root_dir[0] != '\0') ? root_dir : aidogui_res_fs_default_root();
  if (root == nullptr || root[0] == '\0') {
    return false;
  }
  register_driver_once();
  g_root = root;
  g_mounted = true;
  return true;
}

void aidogui_res_fs_unmount() {
  // 不断开驱动注册：LVGL 8.3 的驱动表只增不减，重新注册会留下悬空指针。
  g_mounted = false;
  g_root.clear();
}

bool aidogui_res_fs_mounted() {
  return g_mounted;
}

const char* aidogui_res_fs_root() {
  return g_mounted ? g_root.c_str() : nullptr;
}

AidoguiResFsStats aidogui_res_fs_stats() {
  return g_stats;
}

}  // namespace embark::platform
