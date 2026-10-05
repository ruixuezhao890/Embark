/**
 * Embark · 启动器 App（issue 16、ADR 0006）
 *
 * 主屏：一个中心在底部、竖直对称的扇形槽位架（240×320 竖屏）。每槽 = 一个 App，
 * 槽 0 = 本 App 自己（点它就是 request_switch(0)，框架 no-op，天然安全）。
 * 拖动扇面选槽，点选中槽启动，点非选中槽只把扇面转正。
 *
 * 几何与交互模型全部来自 <embark/launcher_geometry.h>（纯数学、不碰 LVGL，
 * 单元测试直接测它，见 tests/kernel/test_launcher_geometry.cpp）：
 *
 *   - 支点 (120, 270)、半径 82、槽距 STEP = 26°；选中槽永远停在正上方（0°）；
 *   - 拖动 1 px ≈ 0.1 槽（dy，往上拖 = 槽号增大）；滚轮/按键 = 目标槽 ±1；
 *   - 弹簧：pos += (target - pos) * 0.15，在 5 ms UI 节拍上推进
 *     （本 App 用一枚 5 ms LVGL 定时器驱动，宿主循环 5 ms 一帧）；收敛判据
 *     |target - pos| < 0.01。
 *
 * 交互事件（全屏承接，命中是手算的，子对象一律不可点击）：
 *   LV_EVENT_PRESSED / PRESSING / RELEASED / KEY —— 点按与拖动的区分阈值 tap_slop_px。
 *
 * 视觉：深色科技风，颜色只取自 <embark/design_tokens.h>（禁止魔法颜色）：
 * 虚线弧轨（rail） + 实线选中弧段（accent）；每槽 = 圆形描边（ring） +
 * 图标（App::icon() 的 LV_SYMBOL 码点；没有就取标题首字符兜底，见
 * launcher_geometry.h::first_utf8_char）+ 下方标题；选中槽反色
 * （accent 填充圆 + accent_contrast 图标 + accent 标题）。所有文字用
 * 静态子集字库 embark_zh_14（ASCII + 启动器时钟设置心跳你好悬任务 + 5 个
 * FontAwesome 码点；LV_SYMBOL 码点在字库内，见 tools/font/gen_font.mjs）。
 *
 * 零堆纪律：类内没有堆分配；槽位/文字缓冲全是编译期定长成员数组
 * （max_slots = max_apps，config/embark_limits.h）。
 */
#ifndef EMBARK_APP_LAUNCHER_APP_H
#define EMBARK_APP_LAUNCHER_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>
#include <embark/launcher_geometry.h>

namespace embark::demo {

class LauncherApp final : public App {
 public:
  LauncherApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "launcher"; }
  [[nodiscard]] const char* title() const override { return "启动器"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_HOME; }
  // accent() 默认全局强调色（design_tokens::accent）——启动器不挑色。

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

  // --- 观测（宿主验收用）------------------------------------------------------
  /// 当前扇面浮点位置（槽数单位；0.0 = 槽 0 停在正上方）。
  [[nodiscard]] float position() const noexcept { return pos_; }
  /// 弹簧目标槽（整槽；拖动/按键的目标）。
  [[nodiscard]] float target() const noexcept { return target_; }
  /// 弹簧是否已收敛（|target - pos| < 0.01）。
  [[nodiscard]] bool settled() const noexcept { return !animating_; }
  /// 正上方当前停的是哪个槽（= 选中槽）。
  [[nodiscard]] int selected() const noexcept {
    return embark::launcher::selected_index(pos_, slot_count_);
  }
  /// 槽位总数（= 注册表 App 数）。
  [[nodiscard]] int slot_count() const noexcept { return slot_count_; }
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }
  [[nodiscard]] std::uint32_t requests() const noexcept { return requests_; }

 private:
  /// 槽圆直径（圆中心 = 槽中心）。
  static constexpr lv_coord_t slot_diameter = 32;
  /// 点按命中带：|dist - radius| <= slot_hit_radius 才算点到了槽。
  static constexpr lv_coord_t slot_hit_radius = 22;
  /// 按下到抬起位移小于它 = 点按（否则算拖动）。
  static constexpr lv_coord_t tap_slop_px = 6;
  /// 弧轨采样：-90°..+90° 每 6° 一个点（31 点）。
  static constexpr int rail_point_count = 31;
  /// 选中弧段采样：±13°（= STEP/2，7 点）。
  static constexpr int highlight_point_count = 7;

  static void on_screen_event(lv_event_t* event) noexcept;
  static void on_spring_timer(lv_timer_t* timer) noexcept;

  /// 松开手指后：弹簧目标吸到最近的整槽（拖动路径）。
  void settle_at(float snapped_target) noexcept;
  /// 弹簧目标 = 整槽 target（并夹到 [0, count-1]）。
  void apply_target(float new_target) noexcept;
  /// 命中测试：把屏幕坐标换算回槽号；没点到槽返回 -1。
  [[nodiscard]] int hit_slot(lv_coord_t x, lv_coord_t y) const noexcept;
  /// 一次性建屏：弧轨 / 选中弧段 / 全部槽（圆 + 图标 + 标题）。
  void build_panel() noexcept;
  /// 扇面动了：重摆每个槽的位置和配色（选中态跟随 pos_）。
  void rebuild_panel() noexcept;
  /// 点中选中槽：请求切换到对应 App（槽 0 = 自己，框架 no-op）。
  void enter_app(int slot) noexcept;

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* rail_ = nullptr;
  lv_obj_t* highlight_ = nullptr;
  lv_timer_t* spring_timer_ = nullptr;

  lv_obj_t* slot_roots_[embark::launcher::max_slots] = {};
  lv_obj_t* slot_icons_[embark::launcher::max_slots] = {};
  lv_obj_t* slot_titles_[embark::launcher::max_slots] = {};
  /// 槽图标文字缓冲（UTF-8 首字符，最多 4 字节 + NUL；icon() 为空时用）。
  char slot_icon_text_[embark::launcher::max_slots][8] = {};

  /// lv_line 只存指针：点数组必须是成员（生命周期 = 本 App）。
  lv_point_t rail_points_[rail_point_count] = {};
  lv_point_t highlight_points_[highlight_point_count] = {};

  int slot_count_ = 0;
  float pos_ = 0.0F;        // 扇面浮点位置（槽数单位）
  float target_ = 0.0F;     // 弹簧目标（整槽）
  bool animating_ = false;  // 弹簧还没收敛
  bool dragging_ = false;   // 手指按住中（弹簧不跑）

  // 拖动/点按状态（LV_EVENT_PRESSED 记录，RELEASED 结算）。
  float drag_start_pos_ = 0.0F;
  lv_coord_t drag_start_y_ = 0;
  lv_coord_t tap_x_ = 0;
  lv_coord_t tap_y_ = 0;

  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
  std::uint32_t requests_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_LAUNCHER_APP_H */
