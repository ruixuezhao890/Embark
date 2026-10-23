/**
 * Embark · 启动器 App（issue 16、ADR 0006；界面现由 AiDoGUI Designer 生成代码提供）
 *
 * 主屏不再是手绘的扇形槽位架：AiDoGUI 工程里有一屏就叫 "launcher"
 * （屏名约定 == App 名，子页 = <app名>_<编号>_sub，见 aidogui_ui_nav.h），
 * 本 App 只负责把那一屏挂上显示器，并让屏上的控件驱动框架切 App：
 *
 *   - onCreate        → ui_backend_attach_nav(fw)：确保生成代码宿主已就位（幂等），
 *                       并装上屏切换观察者——界面里「调用函数」切到某 App = 框架切过去；
 *   - onCreate        → 还声明"主屏装载后重推控件值"（bind_repaint → repaint_screen()）；
 *   - onEnter/onResume → ui_backend_load_for_app("launcher")：装载同名屏（缺失时只记日志）；
 *                       屏装载完由桥回调 repaint_screen()：磁贴重建、时间/天气卡重推、
 *                       恢复到上次停留的那一页；
 *   - onForegroundTick → ui_backend_tick()：给 UI 一帧机会（AiDoGUI 后端是空实现）。
 *
 * 没有后台节拍：屏上那两个时间控件与天气卡是"前台渲染型"——只有人在主屏时才需要跟着
 * 墙钟/城市走，离开主屏后没什么可推进的（ADR 0015 退役了这里的 tick 1 s）。
 *
 * 历史：issue 16 的扇形槽位架（弧轨 + 拖动 + 弹簧 + 手算命中）已随
 * core/include/embark/launcher_geometry.h 与 core/tests/kernel/test_launcher_geometry.cpp 一起
 * 下线；启动能力改由界面屏上的控件经「调用函数」事件驱动。启动器仍是 App 0。
 *
 * 零堆纪律：类内没有堆分配；界面对象都由生成代码持有（见 aidogui_ui_bridge.cpp）。
 */
#ifndef EMBARK_APP_LAUNCHER_APP_H
#define EMBARK_APP_LAUNCHER_APP_H

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

#include "launcher_pages.h"
#include "launcher_tiles.h"

namespace embark::demo {

class LauncherApp final : public App {
 public:
  LauncherApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "launcher"; }
  [[nodiscard]] const char* title() const override { return "启动器"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_HOME; }
  // accent() 默认全局强调色（design_tokens::accent）——启动器不挑色。

  // settings() 不覆盖：用 App 的默认值（suspend = 不申请后台）。理由见上面"没有后台节拍"。

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onExit() override;

  // --- 验收观测点（ui_demo.cpp 的验收模式断言用）----------------------------
  [[nodiscard]] unsigned enters() const { return enters_; }
  [[nodiscard]] unsigned resumes() const { return resumes_; }
  [[nodiscard]] unsigned foreground_ticks() const { return foreground_ticks_; }

 private:
  /// 主屏装载后的重推钩子（桥在每次装载完新屏后调用，见 aidogui_ui_nav_bind_repaint）。
  /// 屏每次都是**新对象**：磁贴要重新长出来、时间/天气卡要重推、还要跳回上次停留的页。
  /// 顺序 = 清缓存 → 重推值 → 重建磁贴 → 恢复页（磁贴是控件来源，最后恢复页最稳）。
  void repaint_screen();

  /// bind_repaint 收的是裸函数指针（不能捕获 this）：转发到当前实例（见 ui_backend.h 的声明）。
  static void on_screen_reloaded();

  static LauncherApp* g_for_ui;  // 当前接线的启动器实例（进程内只有一个）

  /// 按注册表重建主屏磁贴：磁贴数/文案/顺序都从 fw_->apps() 来（跳过第 0 项 = 主屏自己），
  /// 屏每次装载后都要调（控件是新的对象，磁贴得重新长出来）。
  void rebuild_tiles();

  /// 把本地墙钟写进屏上的 L_Time（大表）/ L_Clock0（右上角小表），以及日期行 L_Date
  /// （「2026年10月17日 星期六」—— 设计稿初值是画死的某一天，必须由这里覆盖）。
  /// 只在值变化时写（时间看分钟、日期看天）；控件不存在（屏未装载）时静默跳过。
  void refresh_clock();

  /// 把主屏天气卡（Card_Weather 的四个文案）刷成 hal().storage 里记住的那个城市。
  /// 数据桩与天气屏同源（weather/weather_data.h）：天气卡只是同一份数据的另一种画法。
  void refresh_weather();

  LauncherTile tiles_[launcher::max_tiles] = {};  // 磁贴元数据（字符串是静态字面量）
  std::size_t tile_count_ = 0;  // 本次重建写进 tiles_ 的条数（= 屏上磁贴数）
  unsigned enters_ = 0;
  unsigned resumes_ = 0;
  unsigned foreground_ticks_ = 0;
  std::size_t page_ = 0;  // 视图留存：离开主屏时停在第几页（onPause 记下，repaint 跳回）
  // 上次刷新的分钟（同分钟不重写）；repaint_screen 会清成 0xFFFFFFFFU —— 屏是重建的，
  // label 已被打回设计稿初值，不清缓存就会一直停在那个初值上。
  std::uint32_t last_minute_ = 0xFFFFFFFFU;
  // 上次刷新的「本地天数」（1970-01-01 起）。同样每天才重写一次日期行；
  // repaint_screen 清成 INT64_MIN —— 新屏的 L_Date 又回到设计稿那一天了。
  std::int64_t last_day_ = INT64_MIN;
  std::uint8_t shown_city_ = 0xFFU;  // 上次画上卡片的城市（0xFF = 屏刚建、必须重画）
  Framework* fw_ = nullptr;          // onCreate 记下；hal().time 是取墙钟的唯一入口
};

}  // namespace embark::demo

#endif /* EMBARK_APP_LAUNCHER_APP_H */