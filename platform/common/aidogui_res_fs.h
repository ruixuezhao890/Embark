/**
 * AiDoGUI 资源盘（'S'）：把生成代码里写死的 "S:/fonts/<name>.bin" / "S:/images/<name>.bin"
 * 映射到一个真实目录（ADR 0011，替代 EEZ 迁移的落地零件之一）。
 *
 * 为什么自己注册一个盘，而不是打开 LVGL 的 LV_USE_FS_POSIX/LV_USE_FS_STDIO：
 *   1) LVGL 的 POSIX/STDIO 驱动把根目录写进 lv_conf.h 的编译期宏（LV_FS_POSIX_PATH），
 *      而宿主（仓库里的 app/aidogui_ui/export）与真机（SD 卡 / FATFS 挂载点）
 *      的根不是同一个路径，同一份 lv_conf.h 满足不了两端；
 *   2) 开发期还要能临时换根（换工程、验收、A/B 对比），所以根路径得在运行时给。
 *   本驱动只用 stdio：宿主直接读本机目录；真机把它指向 esp_vfs_fat 挂出来的 POSIX
 *   路径即可（step 3 真机立项时接），生成代码与桥一行都不用改。
 *
 * 生成的代码里路径是常量（AiDoGUI 的 C 模板行为，与工程设置无关）：
 *   字体 S:/fonts/<name>.bin、图片 S:/images/<name>.bin
 * 所以挂载根下要有字体目录与图片目录：库里就放 AiDoGUI 导出目录的原样（font/ 与 image/），
 * 本驱动在 `/fonts/` 打不开时会自动回退试 `/font/`（图片同理）——GUI 导出完不用改名。
 *
 * 读写失败只反映成 LVGL 的返回值（NOT_EX / FS_ERR），不在这里打日志刷屏；
 * 字体加载失败由桥记一条警告并降级（见 aidogui_ui_bridge.cpp）。
 */
#ifndef EMBARK_PLATFORM_COMMON_AIDOGUI_RES_FS_H
#define EMBARK_PLATFORM_COMMON_AIDOGUI_RES_FS_H

namespace embark::platform {

/// 挂载 'S' 盘。root_dir 为空/未传时用 aidogui_res_fs_default_root()。
/// 重复调用 = 换根（先卸载再挂）；root 为空串返回 false（保持未挂载）。
bool aidogui_res_fs_mount(const char* root_dir = nullptr);

/// 卸载：之后对 S: 的读一律 LV_FS_RES_NOT_EX。挂过再挂不需要重新注册驱动。
void aidogui_res_fs_unmount();

bool aidogui_res_fs_mounted();

/// 当前挂载根（未挂载返回 nullptr）。指针在校验期内有效。
const char* aidogui_res_fs_root();

/// 默认根：环境变量 EMBARK_AIDOGUI_RES 优先，其次编译期 EMBARK_AIDOGUI_RES_ROOT
/// （宿主 = <仓库>/app/aidogui_ui/export；真机 = 空，step 3 由 SD 挂载点填）。
const char* aidogui_res_fs_default_root();

/// 统计（验收探针用：字体加载成功/失败能对上是「文件找不到」还是「驱动坏了」）。
struct AidoguiResFsStats {
  unsigned opens_ok;
  unsigned opens_failed;
  unsigned reads_ok;
  unsigned reads_failed;
};
AidoguiResFsStats aidogui_res_fs_stats();

}  // namespace embark::platform

#endif /* EMBARK_PLATFORM_COMMON_AIDOGUI_RES_FS_H */
