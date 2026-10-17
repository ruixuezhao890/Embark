/**
 * 宿主 · UI 演示可执行文件（issues/06；issue 16 起点；issue 19 起界面全部由
 * EEZ Studio 生成代码接管 —— 手绘 UI 已退役，App 只剩薄壳逻辑）
 *
 * 从 issue 06 起，这个程序是"唯一 UI 任务"的第一个真实例子：
 *   进程入口 main 只做两件事 —— 起 UI 任务（FreeRTOS 静态任务）+ 进调度器；
 *   真正的启动与主循环全在 ui_main 里（= 那个唯一 UI 任务）：
 *     HAL（SDL 窗口在创建它的线程上泵事件）→ UI 端口（唯一允许调 lv_timer_handler()
*     的地方）→ Framework（App 注册表 + 前台切换）→ 事件循环。
 *
 * 默认前台是 LauncherApp，它的主屏与 clock 屏一样来自 EEZ Studio 生成代码
 * （app/eez_ui/src/ui/；屏名约定 == App 名，子页 = <app名>_<编号>_sub，见
 * eez_ui_bridge.h / eez_ui_nav.h）：EEZ 屏上控件的 Flow SetPage 被桥翻成"切到
 * 同名 App"，所以点 EEZ 屏上的按钮 = 启动那个 App。手绘的扇形启动器与 demo App
 * 手绘按钮已全部下线（亮度改由设置中心 SettingsApp::bump_brightness() 驱动，0..100 百分比）
 *
 * 验收开关分五档（全部由合成 SDL 输入 + 帧号驱动，确定性）：
 *   --click [X,Y]   点 EEZ launcher 屏的按钮（Flow SetPage → clock 屏 → 切 ClockApp），
 *                   再在 clock 屏上点 (X,Y)（默认 = clock 屏按钮中心，回启动器），
 *                   验证点击落点随前台切换走。
 *   --drag X1,Y1,X2,Y2  合成一次拖动（默认在空白处 40,60 → 100,140）：EEZ 屏上
 *                   没有可拖动的槽位了，这里只作输入通路冒烟 —— press → 3 个插值
 *                   move → release 全程不误触按钮、不切 App。
 *   --launch        完整故事线（无人值守验收主路径）：程序化 request_switch("clock")
 *                   → 点 clock 屏按钮回启动器 → 点 launcher 屏按钮再进 clock →
*                   再点 clock 屏按钮回启动器 → settings->bump_brightness(+10)（消息广播）；
 *                   断言 switches==4 与各 App 钩子序。
 *   --ui            AiDoGUI 验收（ADR 0011）：字体报告（期望 6/6）+ 屏表/控件表自检
 *                   （launcher 屏 23 个控件、按名取 L_Time 文本为 HH:MM 形状）→ 释放当前屏
 *                   （控件归零）→ 再装载（恢复 23 个控件）；全程同步调用，不需要点击。
 *   --eez           EEZ 验收（issue 19 / ADR 0008；AiDoGUI 接管 App 层后只作老代码回归）：屏表自检
 *                   + 变量表自检（launcher_tap_count 构建期生成）+ 变量读写往返
 *                   → 点 EEZ 屏按钮驱动切 App（Flow SetPage → 屏名 → request_switch）
 *                   → 点 clock 屏按钮回启动器；断言 switches==2、nav_requests==2、last_screen=="launcher"。
 *   --screenshot FILE 最后一帧把窗口内容存成 BMP（用 Python/Pillow 转 PNG 便于查看）
 *   --frames N / --scale S / --delay MS / --quit-at N / --help 同旧版。
 *
 * 界面文案：demo App 界面与 EEZ 生成屏都用 LVGL 默认字体（Montserrat）；导航壳
 * 已退役（2026-10-06），不再有框架级叠加层；日志照旧是中文。
 * 日志照旧是中文。
 *
 * 退出码：0 = 正常；1 = 后端起不来或用法错误；2 = 验收失败（日志写明哪一步）。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>
#include <lvgl.h>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>
#include <embark_limits.h>

#include "album/album_app.h"
#include "clock/clock_app.h"
#include "eez_ui_bridge.h"
#include "eez_ui_nav.h"
#include "music/music_app.h"
#include "settings/settings_app.h"
#include "settings/settings_prefs.h"
#include "weather/weather_app.h"
#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_res_fs.h"
#include "aidogui_ui_bridge.h"
#include "aidogui_ui_nav.h"
#endif
#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "launcher/launcher_app.h"
#include "launcher/launcher_pages.h"   // 分页/几何的纯函数：验收按它算期望值
#include "launcher/launcher_tiles.h"  // --ui-launcher-pages：运行时磁贴的观测点
#include "lvgl_port.h"
#include "lvgl_ui_port.h"
#include "own_task_spawner.h"
#include "screens.h"  // EEZ 生成的对象表：合成点击按真实控件几何算（见 widget_center）
#include "ui_task.h"
#include "weather/weather_data.h"  // --ui-weather：拿数据桩逐字段比对界面

namespace {

// --- 合成输入的帧号与几何 ----------------------------------------------------
// 每帧 = HAL 一跳（默认 5 ms）。按下后隔两帧抬起，让 LVGL 分两个读周期处理。
// 两个目标控件（app/eez_ui/src/ui/screens.c）：
//   clock 屏「← 返回」按钮 objects.obj0 —— Flow SetPage → launcher 屏；
//   launcher 屏「闹钟」行 objects.app_row_alarm —— launcher 屏上唯一接了 Flow 的控件
//     （SetPage → clock 屏）；它在 apps_list 里，视口 195 高而内容 387 高，
//     初始在屏外 → **必须先滚进视口才点得到**（见 scroll_into_view）。
// 坐标不硬编码：widget_center() 每帧从生成代码的 objects 表读真实几何
// （lv_obj_get_coords），改屏后自动跟着走。下面两个常量只是屏还没建好时的兜底。
constexpr int launcher_button_fallback_x = 163;  // launcher 屏「闹钟」行（app_row_alarm）中心
constexpr int launcher_button_fallback_y = 215;
constexpr int clock_button_fallback_x = 188;  // clock 屏「← 返回」按钮中心
constexpr int clock_button_fallback_y = 295;

// 合成点击：press_frame 按下，隔 click_release_delta 帧抬起（LVGL 分两个读周期）。
constexpr int press_frame = 20;
constexpr int click_release_delta = 2;

// --click 故事线：点 launcher 屏按钮切 clock（帧 20），再点 clock 屏按钮回启动器（帧 70，
//   落在首次切页动画 22–62 之后，避开 LVGL 动画期输入屏蔽）。
constexpr int click_second_frame = 70;
constexpr int click_frames = 90;
// --launch 故事线（程序化启动 + 真点击混合；EEZ 屏按钮引发的切页动画 200ms≈40 帧，
//   期间 LVGL 屏蔽输入（lv_indev.c prev_scr 非空），后续点击须等动画窗口结束再落帧）：
//   帧 20 request_switch("clock")（程序化、无动画）；帧 40 点 clock 屏按钮 → 回启动器，
//   动画 42–82；帧 90 点 launcher 屏按钮 → 再进 clock，动画 92–132；
//   帧 84 settings->bump_brightness(+10)（亮度的逻辑入口，BrightnessMessage 广播到 clock）；
//   帧 140 再点 clock 屏按钮 → 回启动器（EEZ 屏内按钮承担返回，导航壳已退役）。
constexpr int launch_switch_frame = 20;
constexpr int launch_clock_back_frame = 40;
constexpr int launch_launcher_btn_frame = 90;
constexpr int launch_bump_frame = 84;
constexpr int launch_home_frame = 140;
constexpr int launch_quit_frame = 190;
constexpr int launch_frames = 200;
// --eez 故事线（issue 19 / ADR 0008，2026-10-06 精简为三 App）：点 launcher 屏按钮
// （帧 20）→ Flow SetPage 到 clock 屏 → 桥的屏观察者 request_switch("clock") →
// ClockApp 前台；帧 70 点 clock 屏按钮 → Flow SetPage 回 launcher 屏 → 观察者
// request_switch 回启动器（回程由 EEZ 屏内按钮承担，导航壳已退役）；帧 70 落在
// 第一次切页动画（22–62）之后，避免 LVGL 动画期屏蔽输入。
constexpr int eez_switch_frame = press_frame;
constexpr int eez_home_frame = 70;
constexpr int eez_quit_frame = 100;
constexpr int eez_frames = 110;
// --ui 故事线（ADR 0011）：帧 10 一次性跑完 Aidogui 验收（同步调用，无合成输入），
//   帧 30 关窗 —— 中间留几帧把重建出来的屏真渲染出去（配 --shot-frame 10 可抓图）。
constexpr int aidogui_check_frame = 10;
constexpr int aidogui_quit_frame = 30;
constexpr int aidogui_frames = 40;
// --ui-nav 故事线（ADR 0011 第二步）：真点击驱动的双向往返 + 月历翻月 —— 帧 12 点启动器
//   磁贴「时钟」（设计器「调用函数」事件）→ 帧 22 校验 clock 屏真数据上屏 → 帧 30 点 clock
//   「返回」→ 帧 40 校验回 launcher（clock 屏已释放）→ 帧 50 点时钟卡 Card_Clock（C++
//   bind_click 备用路径）再进 clock → 帧 62 校验 + 记录月历基准显示月 → 帧 70 点「>」翻下月
//   → 帧 80 校验显示月 +1 且标题同步 → 帧 88 点「<」翻回 → 帧 96 校验显示月回到基准 →
//   帧 100 关窗。帧距只要盖住"按 2 帧 + 切换在下个 step 边界生效"；AiDoGUI 屏切换没有
//   EEZ 那种 40 帧的切页动画输入屏蔽窗口。
constexpr int uinav_tile_frame = 12;
constexpr int uinav_clock_check_frame = 22;
constexpr int uinav_back_frame = 30;
constexpr int uinav_back_check_frame = 40;
constexpr int uinav_card_frame = 50;
constexpr int uinav_card_check_frame = 62;
constexpr int uinav_next_frame = 70;
constexpr int uinav_next_check_frame = 80;
constexpr int uinav_prev_frame = 88;
constexpr int uinav_prev_check_frame = 96;
constexpr int uinav_quit_frame = 100;
constexpr int uinav_frames = 110;
// --ui-weather 故事线（ADR 0012）：帧 12 点主屏天气卡（设计器「调用函数」→ 天气 App）→
//   帧 22 把天气屏逐字段对数据桩（城市/现在区/5 天逐行）→ 帧 30 点「>」切城市 → 帧 40 再对
//   一遍（换了城市得换数据）+ 断言只切了一次 → 帧 48 点「返回」→ 帧 58 断言主屏天气卡也
//   换成了同一个城市（卡片与天气屏同源）→ 帧 64 关窗。城市名不写死：起始城市来自
//   hal().storage，跨次运行会延续（这本身就是持久化的证据）。
constexpr int uiweather_card_frame = 12;
constexpr int uiweather_in_frame = 22;
constexpr int uiweather_next_frame = 30;
constexpr int uiweather_next_check_frame = 40;
constexpr int uiweather_back_frame = 48;
constexpr int uiweather_card_check_frame = 58;
constexpr int uiweather_quit_frame = 64;
constexpr int uiweather_frames = 74;
// 兜底落点（正常路径读真实几何）：launcher 天气卡 = 14,134 212×56 → 中心 120,162；
//   weather 返回键 = 8,4 64×28 → 中心 40,18；weather 「>」= CityBar(12,36) 内 (184,4) 28×20
//   → 中心 210,50。
constexpr int uiweather_card_fallback_x = 120;
constexpr int uiweather_card_fallback_y = 162;
constexpr int uiweather_back_fallback_x = 40;
constexpr int uiweather_back_fallback_y = 18;
constexpr int uiweather_next_fallback_x = 210;
constexpr int uiweather_next_fallback_y = 50;
// 兜底坐标（读不到控件几何时才用；正常路径是读真实几何，设计器里挪位置也点得中）：
//   launcher 第 1 个磁贴（运行时按注册表建）= 12,202 48×48 → 中心 36,226；launcher 时钟卡 Card_Clock = 14,30
//   212×96 → 中心 120,78；clock 返回键 Btn_Back = 8,4 64×28 → 中心 40,18；月历翻月键
//   「<」「>」= Card_Cal(12,192 系) 内 24×20 → 中心 36,214 / 204,214。
constexpr int uinav_tile_fallback_x = 36;
constexpr int uinav_tile_fallback_y = 226;
constexpr int uinav_card_fallback_x = 120;
constexpr int uinav_card_fallback_y = 78;
constexpr int uinav_back_fallback_x = 40;
constexpr int uinav_back_fallback_y = 18;
constexpr int uinav_prev_fallback_x = 36;
constexpr int uinav_prev_fallback_y = 214;
constexpr int uinav_next_fallback_x = 204;
constexpr int uinav_next_fallback_y = 214;

// --ui-launcher-pages（主屏分页验收）的时间轴与落点。
// 主屏磁贴是按 App 注册表运行时生成的（app/launcher/launcher_tiles.cpp），本模式就是
// "后面加了 App，还划得到、点得进吗" 的机器断言。回程一律走屏上的「返回」按钮
// （上滑返回手势已去掉）。时序：
//   帧 10   磁贴/页/页码点/字形与注册表自检
//   帧 14..34 在磁贴带上横向拖（一帧一个插值点，跟 --drag 同理）
//   帧 44   断言吸附到第 2 页
//   帧 54   点第 5 个磁贴（「相册」，只在第 2 页上）→ 帧 64 断言进了相册屏
//   帧 70   点相册屏「返回」→ 帧 78 断言回到主屏，**且仍停在第 2 页**（视图留存，ADR 0015）
//   帧 84→104 反向拖回第 1 页 → 帧 118 断言吸附到第 1 页
//   帧 124  点第 2 个磁贴（「设置」，第 1 页上）→ 帧 132 断言进了设置屏
//   帧 136→152 在设置屏正文上向上拖 → 帧 158 断言正文真的滚起来（Body 是滚动容器）
//   帧 164  点设置屏「亮度 ＋」→ 帧 170 断言亮度百分比 +10（屏上的按钮真接到了逻辑上）
//   帧 176  点设置屏「返回」→ 帧 186 断言回到主屏
//   帧 194  关窗
constexpr int ulp_check_frame = 10;
constexpr int ulp_drag_press_frame = 14;
constexpr int ulp_drag_release_frame = 34;
constexpr int ulp_page_check_frame = 44;
constexpr int ulp_tile_click_frame = 54;
constexpr int ulp_album_check_frame = 64;
constexpr int ulp_album_back_frame = 70;
constexpr int ulp_back_check_frame = 78;
constexpr int ulp_back_drag_press_frame = 84;  // 回程留存住第 2 页 → 反向拖回第 1 页
constexpr int ulp_back_drag_release_frame = 104;
constexpr int ulp_page1_check_frame =
    118;  // 反向拖 release 在 104，留 14 帧等吸附（正向那次也隔了 10 帧）
constexpr int ulp_settings_tile_frame = 124;
constexpr int ulp_settings_check_frame = 132;
constexpr int ulp_bright_frame = 136;  // 先点「亮度 ＋」再拖：滚到底后亮度卡已滚出视口
constexpr int ulp_bright_check_frame = 142;
constexpr int ulp_scroll_press_frame = 148;  // 设置屏正文向上拖（16 帧插值，一帧 10 px）
constexpr int ulp_scroll_release_frame = 164;
constexpr int ulp_scroll_check_frame = 170;
constexpr int ulp_settings_back_frame = 176;
constexpr int ulp_home_check_frame = 186;
constexpr int ulp_quit_frame = 194;
constexpr int ulp_frames = 200;

// 磁贴带（Band_Apps）在屏上是 0,202 240×56：横向拖动走带子下沿 y=254（避开磁贴本身，
// 免得"拖完"被当成"点磁贴"）。
constexpr int ulp_band_y = 254;
constexpr int ulp_drag_x_from = 200;
constexpr int ulp_drag_x_to = 40;
// 第 2 页吸附后第 5 格磁贴的中心（lv_obj_get_coords 给的是含滚动的屏坐标，此时落在
// 屏上 36,226 —— 和设计稿第 1 页第 1 格同位）；「设置」是注册表第 2 个 App、第 1 页
// 第 2 格，屏上 92,226 —— 反向拖回第 1 页之后才点得到（第 78 帧留存住的是第 2 页）。
constexpr int ulp_tile_fallback_x = 36;
constexpr int ulp_tile_fallback_y = 226;
constexpr int ulp_settings_tile_fallback_x = 92;
constexpr int ulp_settings_tile_fallback_y = 226;
// 屏上「返回」（8,4 64×28 → 中心 40,18）与设置屏「亮度 ＋」（卡 14,18 + 卡内 162,12
// 34×32 → 屏上 179,64；卡片装在 Body 里，Body 在屏上 0,36）：真几何取不到时用这两个兜底。
constexpr int ulp_back_fallback_x = 40;
constexpr int ulp_back_fallback_y = 18;
constexpr int ulp_bright_fallback_x = 179;
constexpr int ulp_bright_fallback_y = 64;
// 设置屏正文（Body）在屏上 0,36 240×276：从正文下部 (120,250) 竖直划到上部 (120,90)。
// 起点要落在"卡片空白处"而不是按钮上 —— 拖动的滚动链是从被按到的控件往上找滚动容器的。
constexpr int ulp_scroll_x = 120;
constexpr int ulp_scroll_y_from = 250;
constexpr int ulp_scroll_y_to = 90;

#if defined(EMBARK_AIDOGUI_UI)
// App 层唯一的界面接缝（app/common/ui_backend.h）在定义本宏时选 AiDoGUI：EEZ 的 nav
// 不再被 App attach，所以 --eez 的切屏断言在本机构建下必然不成立（EEZ 屏也不再装载）。
// 它不是回归：EEZ 版界面已由 AiDoGUI 取代（ADR 0011），--eez 只保留做老代码参照。
constexpr bool kAidoguiOwnsAppLayer = true;
#else
constexpr bool kAidoguiOwnsAppLayer = false;
#endif
// --drag 冒烟：press 15 → 每帧一个插值 move → release 40。
// **一帧一个点**是关键：稀疏的几个 move 会被 LVGL 的吸附/回弹吃掉，列表只挪十几
// 像素就弹回原位；连续 24 个点才滚得动（LVGL 的 scroll_throw 是逐帧低通滤波的）。
constexpr int drag_press_frame = 15;
constexpr int drag_release_frame = 40;
constexpr int drag_frames = 80;

struct Options {
  int frames = 0;
  int scale = 1;
  int delay_ms = 5;
  bool delay_explicit = false;  // 用户显式给了 --delay？没给时按模式补默认值（见 parse_options）
  const char* screenshot = nullptr;
  int shot_frame = 0;  // --shot-frame N FILE：第 N 帧截图（任意模式）
  const char* shot_path = nullptr;
  bool click = false;
  int click_x = clock_button_fallback_x;  // 默认 = clock 屏按钮中心（回启动器）
  int click_y = clock_button_fallback_y;
  bool drag = false;
  int drag_x1 = 40;
  int drag_y1 = 60;  // 默认落在 EEZ 屏的空白处（不碰按钮）
  int drag_x2 = 100;
  int drag_y2 = 140;
  bool launch = false;
  int quit_at = 0;
  bool eez = false;
  bool ui = false;
  bool ui_nav = false;
  bool ui_weather = false;
  bool ui_launcher_pages = false;
};

void print_usage() {
  std::printf(
      "用法：embark_host_ui [选项]\n"
      "  --frames N         跑 N 帧后退出（默认按模式：launch 200 / click 90 / drag 80 / eez 110 / "
      "ui 40 / ui-nav 110 / ui-launcher-pages 132 / 否则 0 = 一直跑到关窗）\n"
      "  --click [X,Y]      点 launcher 屏「闹钟」行切 clock（帧 20，自动滚进视口）→ 帧 70 在 "
      "clock 屏点 (X,Y)（默认按钮中心 %d,%d = 回启动器）\n"
      "  --drag X1,Y1,X2,Y2 合成拖动冒烟（帧 15 按下 → 每帧一个插值点 → 帧 40 抬起；默认 40,60 → "
      "100,140）\n"
      "  --launch            完整验收故事（request_switch('clock')→点屏按钮往返→bump_brightness 消息）\n"
      "  --ui                AiDoGUI 验收（ADR 0011）：字体 6/6 + 屏表/控件表 + "
      "切走即释放/回来即重建\n"
      "  --ui-nav            双向导航 + 月历翻月验收：点磁贴进 clock → 点返回回 launcher → "
      "点时钟卡再进 clock → 点「>」「<」翻月并校验显示月与标题\n"
      "  --ui-weather        天气验收（ADR 0012）：点主屏天气卡进天气屏 → 天气屏 5 天逐字段对桩 → "
      "点「>」切城市 → 点返回 → 主屏天气卡跟着换\n"
      "  --ui-launcher-pages 主屏分页验收：磁贴/页/页码点按注册表生成 → 横向拖到第 2 页 → "
      "点「相册」磁贴进相册屏 → 点「返回」回主屏 → 点「设置」磁贴进设置屏 → 点「亮度 "
      "+1」校验逻辑绑定 → 点「返回」回主屏\n"
      "  --eez               EEZ 老代码回归（issue 19）：屏表/变量表自检 + 变量读写往返 + EEZ "
      "屏点按钮切 App\n"
      "  --screenshot FILE  最后一帧存 BMP\n"
      "  --shot-frame N FILE 第 N 帧存 BMP（配 --eez 等故事线抓中途屏）\n"
      "  --scale S           窗口放大倍数（默认 1 = 240×320 1:1）\n"
      "  --delay MS          每帧间隔（默认 5；选了合成点击/手势的验收模式时自动用 40 ——\n"
      "                      LVGL 约每 30 ms 读一次输入，帧跑得比它快就会把几次按下/抬起\n"
      "                      采样进同一帧，时序飘：拖动看着没吸附、点击晚十几帧才生效。\n"
      "                      显式给值则以你给的为准）\n"
      "  --quit-at N         第 N 帧推关窗事件\n"
      "  --help              打印用法\n",
      clock_button_fallback_x, clock_button_fallback_y);
}

// --- 真实控件几何（从 EEZ 生成代码的 objects 表读）-----------------------------
// 合成点击必须落在控件真正的中心：屏幕随 EEZ 工程重画而移动，硬编码坐标每改一次屏
// 就失效一次。这里只读、不建控件不改样式。
struct PanelPoint {
  int x;
  int y;
  bool ok;
};

/// 控件中心（面板坐标 = 绝对坐标，LVGL 8 的 coords 已是屏坐标）。
PanelPoint widget_center(const lv_obj_t* obj, int fallback_x, int fallback_y) {
  if (obj == nullptr) {
    return PanelPoint{fallback_x, fallback_y, false};
  }
  lv_area_t coords{};
  lv_obj_get_coords(obj, &coords);
  return PanelPoint{(coords.x1 + coords.x2) / 2, (coords.y1 + coords.y2) / 2, true};
}

/// 把滚动容器里的控件滚进视口（launcher 的 apps_list 内容 387 高、视口只有 195，
/// 「闹钟」行初始在屏外 —— 不先滚进去，合成点击永远打不中）。
void scroll_into_view(lv_obj_t* obj) {
  if (obj != nullptr) {
    lv_obj_scroll_to_view(obj, LV_ANIM_OFF);
  }
}

// --- 合成输入（SDL_PushEvent → 真事件队列 → 输入后端）-------------------------
// ⚠️ 帧间隔必须给够（验收模式用 --delay 40）：LVGL 约每 30 ms 读一次输入，帧跑得比它快时
// 一帧里推的"按下 → 移动 → 抬起"会被采样进同一次读取，时序随机器负载飘（同一份验收脚本
// 一趟过、一趟不过）。--delay 40 让每帧都落到一次输入读取里，点击/拖动才可复现。
void push_motion_and_press(int scale, int panel_x, int panel_y) {
  SDL_Event motion{};
  motion.type = SDL_MOUSEMOTION;
  motion.motion.x = panel_x * scale;
  motion.motion.y = panel_y * scale;
  SDL_PushEvent(&motion);

  SDL_Event press{};
  press.type = SDL_MOUSEBUTTONDOWN;
  press.button.button = SDL_BUTTON_LEFT;
  press.button.state = SDL_PRESSED;
  press.button.clicks = 1;
  press.button.x = panel_x * scale;
  press.button.y = panel_y * scale;
  SDL_PushEvent(&press);
}

/// 拖动中间帧只推 motion（不按不抬）：LVGL 读到的还是"按住移动"。
void push_motion(int scale, int panel_x, int panel_y) {
  SDL_Event motion{};
  motion.type = SDL_MOUSEMOTION;
  motion.motion.x = panel_x * scale;
  motion.motion.y = panel_y * scale;
  SDL_PushEvent(&motion);
}

void push_release(int scale, int panel_x, int panel_y) {
  SDL_Event release{};
  release.type = SDL_MOUSEBUTTONUP;
  release.button.button = SDL_BUTTON_LEFT;
  release.button.state = SDL_RELEASED;
  release.button.clicks = 1;
  release.button.x = panel_x * scale;
  release.button.y = panel_y * scale;
  SDL_PushEvent(&release);
}

bool save_screenshot(embark::platform::host::HostDisplay& display, const char* path) {
  SDL_Surface* surface = display.capture_surface();
  if (surface == nullptr) {
    ELOG_ERROR("截图失败：{}", SDL_GetError());
    return false;
  }
  const bool ok = SDL_SaveBMP(surface, path) == 0;
  if (!ok) {
    ELOG_ERROR("截图失败：写文件失败（{}）", SDL_GetError());
  }
  SDL_FreeSurface(surface);
  return ok;
}

}  // namespace

namespace embark::platform::host {

// 整个可执行文件只出现一次的 App 注册表：LauncherApp（启动器）首位 = 默认前台，
// 之后依次是 ClockApp/SettingsApp/WeatherApp/MusicApp/AlbumApp。界面都来自
// AiDoGUI 设计工程（app/aidogui_ui/，屏名 == App 名）；启动器必须第一位。
// 主屏磁贴就是按**这张表**运行时生成的（app/launcher/launcher_tiles.cpp）：
// 加 App = 在这里加一项 + 在 AiDoGUI 里加同名屏，主屏的页数与页码点自己跟着变。
// MusicApp/AlbumApp 是占位 App（屏上写着"界面还没做"）：6 个磁贴 → 主屏 2 页，
// 正好用来验收"后面的 App 划得到"（--ui-launcher-pages）。
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp,
                 embark::demo::WeatherApp, embark::demo::MusicApp, embark::demo::AlbumApp)

}  // namespace embark::platform::host

using embark::Error;
using embark::Framework;

namespace hp = embark::platform::host;

namespace {

bool host_exit_query(void* context) noexcept {
  return static_cast<hp::HostInput*>(context)->quit_requested();
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    auto next_is_flag = [&]() { return i + 1 >= argc || argv[i + 1][0] == '-'; };
    if (std::strcmp(arg, "--frames") == 0 && !next_is_flag()) {
      options.frames = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--scale") == 0 && !next_is_flag()) {
      options.scale = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--delay") == 0 && !next_is_flag()) {
      options.delay_ms = std::atoi(argv[++i]);
      options.delay_explicit = true;
    } else if (std::strcmp(arg, "--screenshot") == 0 && !next_is_flag()) {
      options.screenshot = argv[++i];
    } else if (std::strcmp(arg, "--shot-frame") == 0 && i + 2 < argc) {
      options.shot_frame = std::atoi(argv[++i]);
      options.shot_path = argv[++i];
    } else if (std::strcmp(arg, "--quit-at") == 0 && !next_is_flag()) {
      options.quit_at = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--click") == 0) {
      options.click = true;
      if (!next_is_flag()) {
        int x = 0, y = 0;
        if (std::sscanf(argv[++i], "%d,%d", &x, &y) == 2) {
          options.click_x = x;
          options.click_y = y;
        }
      }
    } else if (std::strcmp(arg, "--drag") == 0) {
      options.drag = true;
      if (!next_is_flag()) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (std::sscanf(argv[++i], "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4) {
          options.drag_x1 = x1;
          options.drag_y1 = y1;
          options.drag_x2 = x2;
          options.drag_y2 = y2;
        }
      }
    } else if (std::strcmp(arg, "--launch") == 0) {
      options.launch = true;
    } else if (std::strcmp(arg, "--eez") == 0) {
      options.eez = true;
    } else if (std::strcmp(arg, "--ui") == 0) {
      options.ui = true;
    } else if (std::strcmp(arg, "--ui-nav") == 0) {
      options.ui_nav = true;
    } else if (std::strcmp(arg, "--ui-weather") == 0) {
      options.ui_weather = true;
    } else if (std::strcmp(arg, "--ui-launcher-pages") == 0) {
      options.ui_launcher_pages = true;
    } else if (std::strcmp(arg, "--help") == 0) {
      print_usage();
      std::exit(0);
    } else {
      // 严格解析：不认识的选项/缺参数直接报错退出（旧版静默忽略会让 --drag
      // 少了坐标时退化成"跑到底不退出"，非常难查）。
      std::fprintf(stderr, "未知选项或缺参数：%s\n", arg);
      print_usage();
      std::exit(1);
    }
  }

  // 合成输入（点击 / 拖动 / 手势）的时序由 LVGL 的输入读取周期决定（约 30 ms）：帧间隔比它
  // 小的时候，几次按下/抬起会被采样进同一帧 —— 拖动看着没吸附、点击晚十几帧才生效。
  // 以前这靠人在命令行上记得加 --delay 40，忘了就得到一条假红（比如
  // --ui-launcher-pages 的两条拖动断言 scroll_x=186/40，期望 240/0）。这里替用户补上：
  // 只要有合成输入、又没显式给 --delay，就用 40 ms（要压测照样可以显式给更小的值）。
  const bool synthetic_input = options.click || options.drag || options.launch || options.eez ||
                               options.ui || options.ui_nav || options.ui_weather ||
                               options.ui_launcher_pages;
  if (synthetic_input && !options.delay_explicit) {
    options.delay_ms = 40;
    std::printf("[ui_demo] 有合成输入的模式：帧间隔自动设为 40 ms（要覆盖请显式传 --delay MS）\n");
  }
  return options;
}

#if defined(EMBARK_AIDOGUI_UI)
/// launcher 屏上时间控件的形状检查：必须是 "HH:MM"（两位时 + 冒号 + 两位分）。
/// LauncherApp::onBackgroundTick 按 PC 墙钟刷这两个 label，所以具体值随时钟变，
/// 验收只校验「形状对 + 不是设计器写死的初值 "11:31"」——后者说明刷新没生效。
bool aidogui_time_text_ok(const char* text) {
  if (text == nullptr)
    return false;
  if (std::strlen(text) != 5)
    return false;
  if (text[2] != ':')
    return false;
  for (int i = 0; i < 5; ++i) {
    if (i == 2)
      continue;
    if (text[i] < '0' || text[i] > '9')
      return false;
  }
  const int hh = (text[0] - '0') * 10 + (text[1] - '0');
  const int mm = (text[3] - '0') * 10 + (text[4] - '0');
  return hh < 24 && mm < 60;
}

/// AiDoGUI 验收（ADR 0011）：字体报告 → 屏表 → 控件表 → 切走即释放 → 回来即重建。
/// 全是同步调用，必须在 UI 任务（唯一允许碰 LVGL 的线程）里跑。返回 true = 全部通过。
bool aidogui_acceptance() {
  bool ok = true;

  // 1) 字体：ADR 0017 之后只有一条路 —— 桥把卡上的 ui_heiti.ttf 整份读进 PSRAM，为设计器
  //    用到的 7 个字号各建一份 tiny_ttf 实例。建不出来时 7 个槽全指向内置 14 px（屏不空但
  //    字号不对），所以这里必须全绿。
  const embark::demo::AidoguiFontReport fonts = embark::demo::aidogui_ui_bridge_font_report();
  const char* res_root = embark::platform::aidogui_res_fs_root();
  ELOG_INFO("aidogui 字体：{}/{} 个字号建出来了（TTF 由桥自己找，资源根 = {}）", fonts.loaded,
            fonts.total, res_root != nullptr ? res_root : "<未挂载>");
  if (fonts.total <= 0 || fonts.loaded != fonts.total) {
    ok = false;
  }

  // 2) 屏表：表来自生成代码（构建期解析），至少一屏且每项都能按名反查回来。
  const int screens = embark::demo::aidogui_ui_bridge_screen_count();
  ELOG_INFO("aidogui 屏表：{} 屏", screens);
  for (int i = 0; i < screens; ++i) {
    const char* name = embark::demo::aidogui_ui_bridge_screen_name(i);
    ELOG_INFO("  aidogui 屏[{}]：{}", i, name != nullptr ? name : "<null>");
    if (name == nullptr || !embark::demo::aidogui_ui_bridge_has_screen(name)) {
      ok = false;
    }
  }

  // 3) 前台 App 的屏已装载（LauncherApp::onEnter → ui_backend_load_for_app("launcher")），
  //    注意：走的不是生成的 aidogui_init()，是桥自己按需建屏。
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool created_before = embark::demo::aidogui_ui_bridge_screen_created("launcher");
  const int widgets = embark::demo::aidogui_ui_bridge_widget_count("launcher");
  ELOG_INFO("aidogui 当前屏：{}（已创建 {}）；控件 {} 个", current != nullptr ? current : "<null>",
            created_before ? "是" : "否", widgets);
  if (current == nullptr || std::strcmp(current, "launcher") != 0 || !created_before ||
      widgets <= 0) {
    ok = false;
  }

  // 4) 按名取控件：这是 App 读写界面数据的入口（替代 EEZ 的 Flow 全局变量）。
  //    文本必须是"HH:MM"形状（launcher 的时间由 LauncherApp::onBackgroundTick 按 PC 墙钟刷，
  //    不再固定是设计器初值 "11:31"）—— 形状不对说明刷新链路断了。
  lv_obj_t* time_label = embark::demo::aidogui_ui_bridge_widget("launcher", "L_Time");
  const char* text = time_label != nullptr ? lv_label_get_text(time_label) : nullptr;
  ELOG_INFO("aidogui 控件按名：launcher/L_Time = \"{}\"", text != nullptr ? text : "<取不到>");
  if (!aidogui_time_text_ok(text)) {
    ok = false;
  }

  // 5) 切走即释放：释放后根控件与每个控件指针都归零（LV_EVENT_DELETE 回调干的）。
  embark::demo::aidogui_ui_bridge_unload_current();
  const bool created_after = embark::demo::aidogui_ui_bridge_screen_created("launcher");
  lv_obj_t* after_unload = embark::demo::aidogui_ui_bridge_widget("launcher", "L_Time");
  ELOG_INFO("aidogui 释放后：已创建 {}；L_Time {}", created_after ? "是" : "否",
            after_unload != nullptr ? "还在" : "已释放");
  if (created_after || after_unload != nullptr) {
    ok = false;
  }

  // 6) 回来即重建：再装载一次，控件重新建出来（LauncherApp 会在下一拍把时间刷成真值）。
  const bool reloaded = embark::demo::aidogui_ui_bridge_load_screen("launcher");
  lv_obj_t* time_label2 = embark::demo::aidogui_ui_bridge_widget("launcher", "L_Time");
  const char* text2 = time_label2 != nullptr ? lv_label_get_text(time_label2) : nullptr;
  ELOG_INFO("aidogui 重新装载：{}；控件 {} 个；L_Time = \"{}\"", reloaded ? "成功" : "失败",
            embark::demo::aidogui_ui_bridge_widget_count("launcher"),
            text2 != nullptr ? text2 : "<取不到>");
  if (!reloaded || !aidogui_time_text_ok(text2)) {
    ok = false;
  }

  return ok;
}

/// --ui-nav：合成点击的落点 —— 读 AiDoGUI 控件的真实几何（geo 变了也点得中），读不到才用兜底。
PanelPoint aidogui_widget_center(const char* screen, const char* widget, int fallback_x,
                                 int fallback_y) {
  return widget_center(embark::demo::aidogui_ui_bridge_widget(screen, widget), fallback_x,
                       fallback_y);
}

/// --ui-nav 帧 22：点完磁贴应该停在 clock 屏，而且 App 已经把真数据推上去了。
bool aidogui_clock_check() {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool created = embark::demo::aidogui_ui_bridge_screen_created("clock");
  const int widgets = embark::demo::aidogui_ui_bridge_widget_count("clock");
  lv_obj_t* time_label = embark::demo::aidogui_ui_bridge_widget("clock", "L_Time");
  const char* time_text = time_label != nullptr ? lv_label_get_text(time_label) : nullptr;
  lv_obj_t* bar = embark::demo::aidogui_ui_bridge_widget("clock", "Bar_Prog");
  const int progress = bar != nullptr ? static_cast<int>(lv_bar_get_value(bar)) : -1;
  ELOG_INFO(
      "ui-nav 点磁贴后：当前屏 {}（clock 已创建 {}，控件 {} 个）；L_Time = \"{}\"; Bar_Prog = {}",
      current != nullptr ? current : "<null>", created ? "是" : "否", widgets,
      time_text != nullptr ? time_text : "<取不到>", progress);
  bool ok = current != nullptr && std::strcmp(current, "clock") == 0 && created && widgets > 0;
  // 数据真上屏了吗：时间是 App 写的真时间（含 ":"、没有占位的 "--"），进度条在 1..365
  // （设计器初值 value=0、占位文案是 "--##6ea0ff :#..."，都对不上）。
  if (time_text == nullptr || std::strstr(time_text, ":") == nullptr ||
      std::strstr(time_text, "--") != nullptr) {
    ok = false;
  }
  if (progress <= 0 || progress > 365) {
    ok = false;
  }
  // 导航命中：这一跳来自主屏运行时磁贴（launcher_tiles 的点击 → aidogui_ui_nav_request_app）。
  // C++ 侧 bind_click 还有一条备用绑定（launcher/Card_Clock → clock），本跳不经过它。
  const int hits = embark::demo::aidogui_ui_nav_click_hits();
  const int bindings = embark::demo::aidogui_ui_nav_click_bindings();
  ELOG_INFO("ui-nav 导航命中 {} 次（C++ 侧点击绑定 {} 条；本跳由设计器事件驱动）", hits, bindings);
  if (hits != 1) {
    ok = false;
  }
  return ok;
}

/// --ui-nav 帧 40：点完「返回」应该回到 launcher，而且 clock 屏已经被释放（切走即销毁）。
bool aidogui_back_check() {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool launcher_created = embark::demo::aidogui_ui_bridge_screen_created("launcher");
  const bool clock_alive = embark::demo::aidogui_ui_bridge_screen_created("clock");
  lv_obj_t* time_label = embark::demo::aidogui_ui_bridge_widget("launcher", "L_Time");
  const char* time_text = time_label != nullptr ? lv_label_get_text(time_label) : nullptr;
  const int hits = embark::demo::aidogui_ui_nav_click_hits();
  ELOG_INFO(
      "ui-nav 点返回后：当前屏 {}（launcher 已创建 {}，clock 已释放 {}）；launcher/L_Time = "
      "\"{}\"; 导航命中 {} 次",
      current != nullptr ? current : "<null>", launcher_created ? "是" : "否",
      clock_alive ? "否" : "是", time_text != nullptr ? time_text : "<取不到>", hits);
  return current != nullptr && std::strcmp(current, "launcher") == 0 && launcher_created &&
         !clock_alive && aidogui_time_text_ok(time_text) && hits == 2;
}

/// 读 clock 屏月历控件（真 lv_calendar）当前的「显示月」；控件不在 / 读不到返回 false。
bool aidogui_calendar_shown(int& y, int& m) {
  lv_obj_t* cal = embark::demo::aidogui_ui_bridge_widget("clock", "Cal");
  if (cal == nullptr)
    return false;
  const lv_calendar_date_t* shown = lv_calendar_get_showed_date(cal);
  if (shown == nullptr)
    return false;
  y = static_cast<int>(shown->year);
  m = static_cast<int>(shown->month);
  return true;
}

/// 卡 2 标题 L_MH 应等于「<显示月>年<显示月>月」（标题跟着视图月走，翻月时同拍切换）。
bool aidogui_month_title_ok(int y, int m) {
  lv_obj_t* label = embark::demo::aidogui_ui_bridge_widget("clock", "L_MH");
  if (label == nullptr)
    return false;
  const char* text = lv_label_get_text(label);
  if (text == nullptr)
    return false;
  char expect[24];
  std::snprintf(expect, sizeof expect, "%d年%d月", y, m);
  return std::strcmp(text, expect) == 0;
}

/// --ui-nav 帧 62：点时钟卡 Card_Clock（C++ bind_click 备用路径）应再次停在 clock 屏；
/// 同时记下月历基准显示月（= 今天所在月，刚进屏时 set_showed_date 落到它），供翻月对比。
bool aidogui_card_check(int& base_y, int& base_m) {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool created = embark::demo::aidogui_ui_bridge_screen_created("clock");
  const int hits = embark::demo::aidogui_ui_nav_click_hits();
  ELOG_INFO("ui-nav 点时钟卡后：当前屏 {}（clock 已创建 {}）；导航命中 {} 次",
            current != nullptr ? current : "<null>", created ? "是" : "否", hits);
  bool ok = current != nullptr && std::strcmp(current, "clock") == 0 && created && hits == 3;
  if (!aidogui_calendar_shown(base_y, base_m)) {
    ELOG_INFO("ui-nav 月历基准显示月：<读不到>");
    ok = false;
  } else {
    ELOG_INFO("ui-nav 月历基准显示月：{}-{}", base_y, base_m);
  }
  return ok;
}

/// --ui-nav 帧 80：点「>」后显示月应 +1（带年进位），卡 2 标题同步跟着走。
bool aidogui_next_check(int base_y, int base_m) {
  int y = 0;
  int m = 0;
  const bool shown_ok = aidogui_calendar_shown(y, m);
  const bool title_ok = aidogui_month_title_ok(y, m);
  int expect_y = base_y;
  int expect_m = base_m + 1;
  if (expect_m > 12) {
    expect_m = 1;
    ++expect_y;
  }
  ELOG_INFO("ui-nav 点「>」后：显示月 {}-{}（期望 {}-{}）；标题{}", shown_ok ? y : -1,
            shown_ok ? m : -1, expect_y, expect_m, title_ok ? "同步" : "不同步");
  return shown_ok && y == expect_y && m == expect_m && title_ok;
}

/// --ui-nav 帧 96：点「<」后显示月应回到基准月（= 今天所在月）。
bool aidogui_prev_check(int base_y, int base_m) {
  int y = 0;
  int m = 0;
  const bool shown_ok = aidogui_calendar_shown(y, m);
  const bool title_ok = aidogui_month_title_ok(y, m);
  ELOG_INFO("ui-nav 点「<」后：显示月 {}-{}（期望 {}-{}）；标题{}", shown_ok ? y : -1,
            shown_ok ? m : -1, base_y, base_m, title_ok ? "同步" : "不同步");
  return shown_ok && y == base_y && m == base_m && title_ok;
}
/// 字形覆盖检查：把一段文本逐字符喂给 LVGL 字体表，任何一个字形缺失都算失败。
/// 为什么必须有这条：字体是按 (ttf, 字号) 子集化的（字符表在 screen.aid 的
/// i18n.customCharsetRows 里显式维护），漏一个字符**不会**编译或运行报错 —— 屏幕上只是
/// 一个方框，验收却会"通过"。这里把"方框"变成可断言的失败。where 只进日志。
bool font_covers_text(const lv_font_t* font, const char* text, const char* where) {
  if (font == nullptr || text == nullptr) {
    return false;
  }
  bool ok = true;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0U;) {
    std::uint32_t cp = 0;
    int len = 1;
    if (*p < 0x80U) {
      cp = *p;
    } else if ((*p & 0xE0U) == 0xC0U) {
      cp = *p & 0x1FU;
      len = 2;
    } else if ((*p & 0xF0U) == 0xE0U) {
      cp = *p & 0x0FU;
      len = 3;
    } else {
      cp = *p & 0x07U;
      len = 4;
    }
    for (int i = 1; i < len; ++i) {
      cp = (cp << 6) | (p[i] & 0x3FU);
    }
    p += len;
    lv_font_glyph_dsc_t dsc{};
    if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
      ELOG_INFO(
          "字形缺失：{} 的字 U+{:04X} 不在该字号字体子集里（screen.aid 的 customCharsetRows 要补）",
          where, cp);
      ok = false;
    }
  }
  return ok;
}

/// 按名取控件后查它当前文本的字形覆盖（--ui-weather / --ui-launcher-pages 共用）。
bool aidogui_font_covers(const char* screen, const char* widget) {
  lv_obj_t* obj = embark::demo::aidogui_ui_bridge_widget(screen, widget);
  if (obj == nullptr) {
    return false;
  }
  char where[64];
  std::snprintf(where, sizeof where, "%s/%s", screen, widget);
  return font_covers_text(lv_obj_get_style_text_font(obj, LV_PART_MAIN), lv_label_get_text(obj),
                          where);
}

/// --ui-launcher-pages：第 index 个运行时磁贴的几何中心（屏坐标，已含滚动偏移）。
/// 磁贴没有名字（不是设计器画死的控件，塞不进 aidogui_ui_bridge_widget 那张表），
/// 只能按对象取几何；取不到就用兜底坐标（验收不瞎点）。
PanelPoint aidogui_tile_center(std::size_t index, int fallback_x, int fallback_y) {
  lv_obj_t* tile = embark::demo::launcher_tiles_tile(index);
  if (tile == nullptr) {
    return PanelPoint{fallback_x, fallback_y, false};
  }
  lv_area_t area{};
  lv_obj_get_coords(tile, &area);
  return PanelPoint{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2, true};
}

/// --ui-launcher-pages 帧 10：主屏磁贴/页/页码点与 App 注册表是否一致。
/// 期望值**全部从注册表推导**（磁贴 = App 数 − 1、页 = 每页 4 个向上取整、几何照设计稿）。
/// 以前这里写死"5 个磁贴 / 2 页 / clock,settings,weather,music,album"—— 加一个 App 就得改验收
/// 程序，等于用测试把新 App 挡在门外（2026-10-17 加 about 时正是这么绊了一下）。顺带断"方框字"。
bool aidogui_launcher_pages_check(const embark::AppRegistry& apps) {
  const std::size_t tiles = embark::demo::launcher_tiles_count();
  const std::size_t pages = embark::demo::launcher_tiles_page_count();
  const std::size_t dots = embark::demo::launcher_tiles_dot_count();
  const std::size_t expect_tiles = embark::launcher::tile_count(apps.size());
  const std::size_t expect_pages = embark::launcher::page_count(expect_tiles);
  // 顺序：磁贴 i 必须就是注册表第 i+1 个 App（第 0 个是主屏自己，不给自己磁贴）。
  bool order_ok = tiles == expect_tiles;
  for (std::size_t i = 0; order_ok && i < tiles; ++i) {
    const char* got = embark::demo::launcher_tiles_app(i);
    const embark::App* expect = apps.at(static_cast<embark::AppId>(i + 1U));
    if (got == nullptr || expect == nullptr || std::strcmp(got, expect->name()) != 0) {
      order_ok = false;
    }
  }
  // 「相册」那一格的文案该是 AlbumApp::title()：先按名字找到它在第几格，再看标签文字。
  std::size_t album_tile = tiles;
  for (std::size_t i = 0; i < tiles && album_tile == tiles; ++i) {
    const char* name = embark::demo::launcher_tiles_app(i);
    if (name != nullptr && std::strcmp(name, "album") == 0) {
      album_tile = i;
    }
  }
  lv_obj_t* album_label =
      album_tile < tiles ? embark::demo::launcher_tiles_tile_label(album_tile) : nullptr;
  const char* album_text = album_label != nullptr ? lv_label_get_text(album_label) : nullptr;
  const bool album_ok =
      album_tile < tiles && album_text != nullptr && std::strcmp(album_text, "相册") == 0;
  bool fonts_ok = true;
  for (std::size_t i = 0; i < tiles; ++i) {
    lv_obj_t* label = embark::demo::launcher_tiles_tile_label(i);
    if (label == nullptr) {
      fonts_ok = false;
      continue;
    }
    char where[48];
    std::snprintf(where, sizeof where, "主屏磁贴 %u", static_cast<unsigned>(i));
    if (!font_covers_text(lv_obj_get_style_text_font(label, LV_PART_MAIN), lv_label_get_text(label),
                          where)) {
      fonts_ok = false;
    }
  }
  // 磁贴 x 是相对页容器的（页内 12/68/…）；页容器 x 才是翻页偏移（第 2 页 = 240）。
  lv_obj_t* t0 = embark::demo::launcher_tiles_tile(0);
  lv_obj_t* t1 = embark::demo::launcher_tiles_tile(1);
  lv_obj_t* p1 = embark::demo::launcher_tiles_page(1);
  lv_obj_t* d0 = embark::demo::launcher_tiles_dot(0);
  lv_obj_t* d1 = embark::demo::launcher_tiles_dot(1);
  const int x_t0 = t0 != nullptr ? lv_obj_get_x(t0) : -1;
  const int x_t1 = t1 != nullptr ? lv_obj_get_x(t1) : -1;
  const int x_p1 = p1 != nullptr ? lv_obj_get_x(p1) : -1;
  const int x_d0 = d0 != nullptr ? lv_obj_get_x(d0) : -1;
  const int x_d1 = d1 != nullptr ? lv_obj_get_x(d1) : -1;
  // 几何期望也走纯函数：页内 12/68、第 2 页整体 +240、页码点整行居中（2 个点时 113/121）。
  const bool geo_ok =
      x_t0 == static_cast<int>(embark::launcher::tile_x(0)) &&
      x_t1 == static_cast<int>(embark::launcher::tile_x(1)) &&
      (expect_pages < 2U || x_p1 == static_cast<int>(embark::launcher::page_x(1))) &&
      x_d0 == static_cast<int>(embark::launcher::dot_x(0, expect_pages)) &&
      (expect_pages < 2U || x_d1 == static_cast<int>(embark::launcher::dot_x(1, expect_pages)));
  lv_obj_t* hint = embark::demo::aidogui_ui_bridge_widget("launcher", "L_Hint");
  const char* hint_text = hint != nullptr ? lv_label_get_text(hint) : nullptr;
  const bool hint_ok = hint_text != nullptr && std::strcmp(hint_text, "左右滑动翻页") == 0 &&
                       aidogui_font_covers("launcher", "L_Hint");
  ELOG_INFO(
      "主屏分页自检：磁贴 {}（期望 {}）/ 页 {}（期望 {}）/ 点 {}（应等于页数）；顺序 {}；"
      "「相册」格 {}；几何 {}（磁贴 x {}/{}，第 2 页 x {}，点 x {}/{}）；字形 {}；提示行 {}（\"{}\"）",
      tiles, expect_tiles, pages, expect_pages, dots, order_ok ? "对" : "错",
      album_ok ? "对" : "<不是相册>", geo_ok ? "对" : "错", x_t0, x_t1, x_p1, x_d0, x_d1,
      fonts_ok ? "全在" : "有缺字", hint_ok ? "对" : "错",
      hint_text != nullptr ? hint_text : "<取不到>");
  return tiles == expect_tiles && pages == expect_pages && dots == expect_pages && order_ok &&
         album_ok && geo_ok && fonts_ok && hint_ok;
}

/// --ui-launcher-pages 帧 44：横向拖完应停在第 2 页（吸附式：松手吸附最近页）。
bool aidogui_launcher_page_check() {
  const std::size_t page = embark::demo::launcher_tiles_current_page();
  lv_obj_t* band = embark::demo::launcher_tiles_band();
  const int scroll_x = band != nullptr ? lv_obj_get_scroll_x(band) : 0;
  const int offset = scroll_x < 0 ? -scroll_x : scroll_x;
  ELOG_INFO("主屏横向拖动后：停在第 {} 页（scroll_x={}，期望吸附到第 1 页 = 240px）", page,
            scroll_x);
  return page == 1 && offset == 240;
}

/// --ui-launcher-pages 帧 64：点第 2 页上的「相册」磁贴应进相册屏（launcher 屏随切走释放）。
bool aidogui_launcher_album_check() {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool album_created = embark::demo::aidogui_ui_bridge_screen_created("album");
  const bool launcher_gone = !embark::demo::aidogui_ui_bridge_screen_created("launcher");
  ELOG_INFO("点第 2 页的「相册」磁贴后：当前屏 {}（album 已创建 {}，launcher 已释放 {}）",
            current != nullptr ? current : "<null>", album_created ? "是" : "否",
            launcher_gone ? "是" : "否");
  return current != nullptr && std::strcmp(current, "album") == 0 && album_created && launcher_gone;
}

/// --ui-launcher-pages 帧 78：相册屏点「返回」应回主屏（主屏磁贴重建出来 = 回来真的重画了），
/// 而且**屏重建后仍停在离开时的第 2 页** —— 这是视图留存（App 自持 view state，ADR 0015）：
/// 屏对象是新的、磁贴是从第 1 页重新长出来的，但 App 记得用户停在第几页，装载后跳回去。
/// 回程走屏上的按钮（设计器事件 embark_ui_open_launcher）——上滑返回手势已按需求去掉。
bool aidogui_launcher_back_check(const embark::AppRegistry& apps) {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool launcher_created = embark::demo::aidogui_ui_bridge_screen_created("launcher");
  const bool album_gone = !embark::demo::aidogui_ui_bridge_screen_created("album");
  const std::size_t tiles = embark::demo::launcher_tiles_count();
  const std::size_t page = embark::demo::launcher_tiles_current_page();
  lv_obj_t* band = embark::demo::launcher_tiles_band();
  const int scroll_x = band != nullptr ? lv_obj_get_scroll_x(band) : 0;
  const int offset = scroll_x < 0 ? -scroll_x : scroll_x;
  const bool page_kept = page == 1 && offset == 240;
  ELOG_INFO(
      "相册屏点「返回」后：当前屏 {}（launcher 已重建 {}，album 已释放 {}）；磁贴重新建出 {} 个；"
      "视图留存 = 仍停在第 {} 页（scroll_x={}，期望第 2 页 = 240px，{}）",
      current != nullptr ? current : "<null>", launcher_created ? "是" : "否",
      album_gone ? "是" : "否", tiles, page + 1, scroll_x, page_kept ? "对" : "错");
  return current != nullptr && std::strcmp(current, "launcher") == 0 && launcher_created &&
         album_gone && tiles == embark::launcher::tile_count(apps.size()) && page_kept;
}

/// --ui-launcher-pages 帧 118：反向拖回第 1 页（留存在第 2 页之后，验收脚本自己把页翻回来）。
bool aidogui_launcher_page1_check() {
  const std::size_t page = embark::demo::launcher_tiles_current_page();
  lv_obj_t* band = embark::demo::launcher_tiles_band();
  const int scroll_x = band != nullptr ? lv_obj_get_scroll_x(band) : 0;
  const int offset = scroll_x < 0 ? -scroll_x : scroll_x;
  ELOG_INFO("主屏反向拖动后：停在第 {} 页（scroll_x={}，期望吸附回第 1 页 = 0px）", page + 1,
            scroll_x);
  return page == 0 && offset == 0;
}

/// --ui-launcher-pages 帧 132：点第 1 页的「设置」磁贴应进设置屏（launcher 屏随切走释放）。
bool aidogui_launcher_settings_check() {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const bool settings_created = embark::demo::aidogui_ui_bridge_screen_created("settings");
  const bool launcher_gone = !embark::demo::aidogui_ui_bridge_screen_created("launcher");
  ELOG_INFO("点「设置」磁贴后：当前屏 {}（settings 已创建 {}，launcher 已释放 {}）",
            current != nullptr ? current : "<null>", settings_created ? "是" : "否",
            launcher_gone ? "是" : "否");
  return current != nullptr && std::strcmp(current, "settings") == 0 && settings_created &&
         launcher_gone;
}
/// --ui-launcher-pages 帧 132（进屏之后）：屏上真填了值，不是只画了个壳。三条：
///   ① 亮度显示的是 App 手里的百分比 —— 期望值由调用方传进来（KV 跨进程留存，不能写死 70）；
///   ②「关于本机」首行是「键：值」的形状 —— 这七行从退役的「关于」App 搬进了设置屏；
///   ③ Body 的内容比视口高（422 > 276）—— 它是滚动容器，不只是一张卡。
bool aidogui_settings_panel_check(std::size_t expect_percent) {
  lv_obj_t* bright = embark::demo::aidogui_ui_bridge_widget("settings", "L_BrightVal");
  lv_obj_t* platform = embark::demo::aidogui_ui_bridge_widget("settings", "L_Platform");
  lv_obj_t* body = embark::demo::aidogui_ui_bridge_widget("settings", "Body");
  const char* bright_text = bright != nullptr ? lv_label_get_text(bright) : "";
  const char* platform_text = platform != nullptr ? lv_label_get_text(platform) : "";
  const int scroll_bottom = body != nullptr ? static_cast<int>(lv_obj_get_scroll_bottom(body)) : -1;
  char expect_text[8] = {};
  std::snprintf(expect_text, sizeof(expect_text), "%u%%", static_cast<unsigned>(expect_percent));
  const bool bright_ok = std::strcmp(bright_text, expect_text) == 0;
  const bool about_ok = platform_text[0] != '\0' && std::strstr(platform_text, "：") != nullptr;
  const bool scroll_ok = scroll_bottom > 0;
  ELOG_INFO(
      "设置屏内容：亮度「{}」（期望 {}）{}；关于首行「{}」（非空且含「：」）{}；正文剩余可滚 {} px"
      "（期望 > 0）{}",
      bright_text, expect_text, bright_ok ? "" : " ✘", platform_text, about_ok ? "" : " ✘",
      scroll_bottom, scroll_ok ? "" : " ✘");
  return bright_ok && about_ok && scroll_ok;
}

/// --ui-launcher-pages 帧 164：设置屏的「亮度 ＋」是屏内按钮（App 侧 dispatcher）→
/// SettingsApp::bump_brightness(+10)，点一下亮度百分比就该 +10 —— 验收「屏上的按钮真接到了
/// 逻辑上」，而不是只画了个能按的壳。
bool aidogui_launcher_bright_check(std::size_t before, std::size_t after) {
  const std::size_t expect =
      embark::demo::clamp_percent(static_cast<int>(before) + embark::demo::kBrightStep);
  const bool ok = after == expect;
  ELOG_INFO("设置屏点「亮度 ＋」后：亮度百分比 {}% → {}%（期望 {}%）{}", before, after, expect,
            ok ? "" : " ✘");
  return ok;
}

/// 两端钳位是纯函数行为，不用加帧就能验：100% 再 ＋ 还是 100%，0% 再 − 还是 0%
/// （旧口径 0..3 四档是循环的，新口径是钳位的 —— 这两条断言就是在盯这个区别）。
bool aidogui_launcher_bright_clamp_check() {
  const std::size_t top = embark::demo::clamp_percent(100 + embark::demo::kBrightStep);
  const std::size_t bottom = embark::demo::clamp_percent(0 - embark::demo::kBrightStep);
  const bool ok = top == 100U && bottom == 0U;
  ELOG_INFO("亮度钳位：100% ＋{} → {}%（期望 100），0% −{} → {}%（期望 0）{}",
            embark::demo::kBrightStep, top, embark::demo::kBrightStep, bottom, ok ? "" : " ✘");
  return ok;
}

/// --ui-launcher-pages 帧 158：在设置屏正文上向上拖，正文应该真的滚下去 —— 也就是
/// "Body 这个滚动容器能被手指拖到"。断言滚过的距离够大（正文 422 px、视口 276 px，
/// 全滚到头是 146 px），顺带把剩余可滚距离打出来（拖到底应为 0）。
bool aidogui_settings_scroll_check(int scroll_before, int scroll_after) {
  lv_obj_t* body = embark::demo::aidogui_ui_bridge_widget("settings", "Body");
  const int before = scroll_before < 0 ? -scroll_before : scroll_before;
  const int after = scroll_after < 0 ? -scroll_after : scroll_after;
  const int remaining = body != nullptr ? lv_obj_get_scroll_bottom(body) : -1;
  const bool ok = body != nullptr && after >= 100 && after > before;
  ELOG_INFO("设置屏正文上拖后：滚动 {} px → {} px（期望 ≥100；剩余可滚 {} px）{}", before, after,
            remaining, ok ? "" : " ✘");
  return ok;
}

/// --ui-launcher-pages 帧 118：设置屏点「返回」应回主屏，且磁贴重新建出。
bool aidogui_launcher_home_check(const embark::AppRegistry& apps) {
  const char* current = embark::demo::aidogui_ui_bridge_current_screen();
  const std::size_t tiles = embark::demo::launcher_tiles_count();
  ELOG_INFO("设置屏点「返回」后：当前屏 {}（应回到 launcher）；磁贴 {} 个",
            current != nullptr ? current : "<null>", tiles);
  return current != nullptr && std::strcmp(current, "launcher") == 0 &&
         tiles == embark::launcher::tile_count(apps.size());
}

/// --ui-weather：天气屏是否与数据桩逐字段一致（城市名 / 现在区 / 5 天逐行）。
/// 城市不写死：比的是"屏幕上的字 == 数据桩里那座城市的字"，起始城市由持久化决定。
bool aidogui_weather_screen_ok(std::size_t city) {
  auto text_of = [](const char* widget) -> const char* {
    lv_obj_t* obj = embark::demo::aidogui_ui_bridge_widget("weather", widget);
    return obj != nullptr ? lv_label_get_text(obj) : nullptr;
  };
  auto eq = [](const char* got, const char* want) {
    return got != nullptr && want != nullptr && std::strcmp(got, want) == 0;
  };
  const embark::demo::WeatherCity& c = embark::demo::weather_city(city);
  const bool head_ok = eq(text_of("L_City"), c.name) && eq(text_of("L_NIcon"), c.days[0].icon) &&
                       eq(text_of("L_NTemp"), c.temp) && eq(text_of("L_NDesc"), c.days[0].desc);
  bool rows_ok = true;
  char widget[32];
  for (std::size_t i = 0; i < embark::demo::kWeatherDayCount; ++i) {
    const embark::demo::WeatherDay& d = c.days[i];
    std::snprintf(widget, sizeof widget, "L_D%u_Dow", static_cast<unsigned>(i));
    if (!eq(text_of(widget), d.dow)) {
      rows_ok = false;
    }
    std::snprintf(widget, sizeof widget, "L_D%u_Icon", static_cast<unsigned>(i));
    if (!eq(text_of(widget), d.icon)) {
      rows_ok = false;
    }
    std::snprintf(widget, sizeof widget, "L_D%u_Desc", static_cast<unsigned>(i));
    if (!eq(text_of(widget), d.desc)) {
      rows_ok = false;
    }
    std::snprintf(widget, sizeof widget, "L_D%u_Range", static_cast<unsigned>(i));
    if (!eq(text_of(widget), d.range)) {
      rows_ok = false;
    }
  }
  bool glyphs_ok = true;
  char check[32];
  for (const char* w : {"L_City", "L_NIcon", "L_NTemp", "L_NDesc"}) {
    if (!aidogui_font_covers("weather", w)) {
      glyphs_ok = false;
    }
  }
  for (std::size_t i = 0; i < embark::demo::kWeatherDayCount; ++i) {
    for (const char* suffix : {"Dow", "Icon", "Desc", "Range"}) {
      std::snprintf(check, sizeof check, "L_D%u_%s", static_cast<unsigned>(i), suffix);
      if (!aidogui_font_covers("weather", check)) {
        glyphs_ok = false;
      }
    }
  }
  const bool created = embark::demo::aidogui_ui_bridge_screen_created("weather");
  const int widgets = embark::demo::aidogui_ui_bridge_widget_count("weather");
  ELOG_INFO("ui-weather 天气屏：已创建 {}；控件 {} 个；城市 \"{}\"；现在 {} {}；5 天逐行{}；字形{}",
            created ? "是" : "否", widgets,
            text_of("L_City") != nullptr ? text_of("L_City") : "<取不到>",
            text_of("L_NTemp") != nullptr ? text_of("L_NTemp") : "<取不到>",
            text_of("L_NIcon") != nullptr ? text_of("L_NIcon") : "<取不到>",
            rows_ok ? "一致" : "不一致", glyphs_ok ? "齐全" : "有缺");
  return created && widgets > 0 && head_ok && rows_ok && glyphs_ok;
}

/// --ui-weather：主屏天气卡是否与同一个城市一致（卡片不是第二份数据）。
bool aidogui_weather_card_ok(std::size_t city) {
  auto text_of = [](const char* widget) -> const char* {
    lv_obj_t* obj = embark::demo::aidogui_ui_bridge_widget("launcher", widget);
    return obj != nullptr ? lv_label_get_text(obj) : nullptr;
  };
  auto eq = [](const char* got, const char* want) {
    return got != nullptr && want != nullptr && std::strcmp(got, want) == 0;
  };
  const embark::demo::WeatherCity& c = embark::demo::weather_city(city);
  bool glyphs_ok = true;
  for (const char* w : {"L_WCity", "L_WTemp", "L_WIcon", "L_WDesc"}) {
    if (!aidogui_font_covers("launcher", w)) {
      glyphs_ok = false;
    }
  }
  // 图标盒要装得下"任何城市"的今日天气字：两字词（雷雨/多云/小雨…）在 22px 下约 44px，
  // 盒子窄了 LVGL 就折行、第二行被裁（2026-10-16 把 L_WIcon 从 30 加宽到 46 就是为这个）。
  // 这里直接量文本宽度，把"视觉上会不会折行"变成断言。
  lv_obj_t* icon = embark::demo::aidogui_ui_bridge_widget("launcher", "L_WIcon");
  const lv_font_t* icon_font =
      icon != nullptr ? lv_obj_get_style_text_font(icon, LV_PART_MAIN) : nullptr;
  lv_coord_t widest = 0;
  const char* widest_text = "";
  if (icon_font != nullptr) {
    for (std::size_t i = 0; i < embark::demo::kWeatherCityCount; ++i) {
      const char* text = embark::demo::weather_city(i).days[0].icon;
      const lv_coord_t w = lv_txt_get_width(text, static_cast<std::uint32_t>(std::strlen(text)),
                                            icon_font, 0, LV_TEXT_FLAG_NONE);
      if (w > widest) {
        widest = w;
        widest_text = text;
      }
    }
  }
  const lv_coord_t icon_box = icon != nullptr ? lv_obj_get_content_width(icon) : -1;
  const bool icon_fits = icon_box > 0 && widest > 0 && icon_box >= widest;
  const bool ok = eq(text_of("L_WCity"), c.name) && eq(text_of("L_WTemp"), c.temp) &&
                  eq(text_of("L_WIcon"), c.days[0].icon) &&
                  eq(text_of("L_WDesc"), c.days[0].desc) && glyphs_ok && icon_fits;
  ELOG_INFO("ui-weather 主屏天气卡：\"{}\" {} {} {}（期望 \"{}\" {} {} {}）{}",
            text_of("L_WCity") != nullptr ? text_of("L_WCity") : "?",
            text_of("L_WTemp") != nullptr ? text_of("L_WTemp") : "?",
            text_of("L_WIcon") != nullptr ? text_of("L_WIcon") : "?",
            text_of("L_WDesc") != nullptr ? text_of("L_WDesc") : "", c.name, c.temp, c.days[0].icon,
            c.days[0].desc, ok ? "" : " ✘");
  ELOG_INFO("ui-weather 主屏天气卡图标盒：宽 {}，最长今日天气字 \"{}\" 量得 {}px（装得下：{}）",
            icon_box, widest_text, widest, icon_fits ? "是" : "否 ✘");
  return ok;
}
#endif /* EMBARK_AIDOGUI_UI */
}  // namespace

/// 唯一 UI 任务（spec §6：唯一能碰 LVGL 的线程，窗口事件也在它上面泵）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(static_cast<std::uint8_t>(options->scale), "Embark 宿主 UI");
  hp::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);

  if (const Error hal_error = hal.init(); hal_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
    hp::exit_process(1);
  }

  embark::platform::LvglUiPort ui_port(hal.context(), &host_exit_query, &input);
  static hp::HostTaskSpawner spawner;
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "框架启动失败：%s\n", embark::error_text(text, boot_error));
    hp::exit_process(1);
  }

  ELOG_INFO("框架就绪：{} 个 App，默认前台 {}（上限 {}）", framework.apps().size(),
            framework.apps().at(0)->name(), embark::max_apps);

  const auto* weather = static_cast<const embark::demo::WeatherApp*>(framework.app(3));

  // --- 按模式定帧数上限（0 = 一直跑到关窗）-----------------------------------
  int frames_limit = options->frames;
  if (frames_limit <= 0) {
    if (options->launch) {
      frames_limit = launch_frames;
    } else if (options->click) {
      frames_limit = click_frames;
    } else if (options->drag) {
      frames_limit = drag_frames;
    } else if (options->eez) {
      frames_limit = eez_frames;
    } else if (options->ui) {
      frames_limit = aidogui_frames;
    } else if (options->ui_nav) {
      frames_limit = uinav_frames;
    } else if (options->ui_weather) {
      frames_limit = uiweather_frames;
    } else if (options->ui_launcher_pages) {
      frames_limit = ulp_frames;
    }
  }

  int frames_run = 0;
  bool aidogui_ok = false;      // --ui：aidogui_acceptance() 的结论（帧 10 才写）
  bool uinav_clock_ok = false;  // --ui-nav：点磁贴后停在 clock 屏且数据上屏（帧 22）
  bool uinav_back_ok = false;  // --ui-nav：点返回后回 launcher 且 clock 屏已释放（帧 40）
  bool uinav_card_ok = false;  // --ui-nav：点时钟卡再进 clock + 记月历基准月（帧 62）
  bool uinav_next_ok = false;  // --ui-nav：点「>」显示月 +1、标题同步（帧 80）
  bool uinav_prev_ok = false;  // --ui-nav：点「<」显示月回到基准（帧 96）
  int uinav_base_y = 0;        // --ui-nav：月历基准显示月（帧 62 记录）
  int uinav_base_m = 0;
  bool uiweather_in_ok = false;  // --ui-weather：进天气屏后 5 天与数据桩一致（帧 22）
  bool uiweather_next_ok = false;  // --ui-weather：点「>」换城市后屏上数据跟着换（帧 40）
  bool uiweather_card_ok = false;  // --ui-weather：回主屏后天气卡换成同一个城市（帧 58）
  bool ulp_tiles_ok = false;  // --ui-launcher-pages：磁贴/页/点/几何/字形自检（帧 10）
  bool ulp_page_ok = false;   // --ui-launcher-pages：拖到第 2 页并吸附住（帧 44）
  bool ulp_album_ok = false;  // --ui-launcher-pages：点第 2 页的相册磁贴进相册屏（帧 64）
  bool ulp_back_ok = false;  // --ui-launcher-pages：相册屏点「返回」回主屏 + 视图留存（帧 78）
  bool ulp_page1_ok = false;  // --ui-launcher-pages：反向拖回第 1 页（帧 118）
  bool ulp_settings_ok = false;  // --ui-launcher-pages：点「设置」磁贴进设置屏（帧 132）
  bool ulp_scroll_ok = false;    // --ui-launcher-pages：设置屏正文能拖起来（帧 158）
  int ulp_scroll_before = 0;     //   拖动前的滚动位置（Body 的 scroll_y）
  int ulp_scroll_after = 0;      //   拖动后的滚动位置
  bool ulp_panel_ok = false;   // --ui-launcher-pages：设置屏真填了值 + 正文可滚（帧 132）
  bool ulp_bright_ok = false;  // --ui-launcher-pages：点设置屏「亮度 ＋」→ 百分比 +10（帧 164）
  bool ulp_home_ok = false;  // --ui-launcher-pages：设置屏点「返回」回主屏（帧 160）
  int ulp_tile_x = 0;        // --ui-launcher-pages：磁贴落点（按下/抬起同一个点）
  int ulp_tile_y = 0;
  int ulp_back_x = 0;  // --ui-launcher-pages：「返回」按钮落点
  int ulp_back_y = 0;
  int ulp_bright_x = 0;  // --ui-launcher-pages：「亮度 +1」按钮落点
  int ulp_bright_y = 0;
  int ulp_bright_before = -1;  // --ui-launcher-pages：进设置屏时的亮度百分比（帧 132 记录）
  int launch_bright_before = -1;  // --launch：bump 前的亮度百分比（KV 会跨进程留存，不能写死 70）
  // --launch：按下点的落点要在抬起帧复用（屏可能在两帧之间切走，届时读不到控件）
  PanelPoint launch_back_pt{0, 0, false};
  PanelPoint launch_row_pt{0, 0, false};
  for (;;) {
    framework.step();
    ++frames_run;
    if (framework.exit_requested()) {
      break;
    }

    // --- 合成输入（帧号驱动，确定性）-----------------------------------------
    // 每帧重新读一次真实几何：屏会随前台切换换掉，坐标必须跟着走。
    const PanelPoint clock_btn =
        widget_center(objects.obj0, clock_button_fallback_x, clock_button_fallback_y);
    const PanelPoint alarm_row = widget_center(objects.app_row_alarm, launcher_button_fallback_x,
                                               launcher_button_fallback_y);

    if (options->launch) {
      // 启动器与各屏都是 AiDoGUI 生成的：往返点击的落点必须从 AiDoGUI 控件表读真几何
      // （objects.* 是 EEZ 时代的空指针，退回兜底坐标会点到空白处 —— 这条故事曾因此静默失效）。
      if (frames_run == launch_switch_frame) {
        (void)framework.request_switch("clock");
      }
      if (frames_run == launch_clock_back_frame) {
        launch_back_pt =
            aidogui_widget_center("clock", "Btn_Back", clock_button_fallback_x, clock_button_fallback_y);
        push_motion_and_press(options->scale, launch_back_pt.x, launch_back_pt.y);
      }
      if (frames_run == launch_clock_back_frame + click_release_delta) {
        push_release(options->scale, launch_back_pt.x, launch_back_pt.y);
      }
      if (frames_run == launch_launcher_btn_frame) {
        // 主屏上接到 clock 的控件是时间卡（磁贴由运行时建，不在控件表里）。
        launch_row_pt = aidogui_widget_center("launcher", "Card_Clock", launcher_button_fallback_x,
                                              launcher_button_fallback_y);
        push_motion_and_press(options->scale, launch_row_pt.x, launch_row_pt.y);
      }
      if (frames_run == launch_launcher_btn_frame + click_release_delta) {
        push_release(options->scale, launch_row_pt.x, launch_row_pt.y);
      }
      if (frames_run == launch_home_frame) {
        launch_back_pt =
            aidogui_widget_center("clock", "Btn_Back", clock_button_fallback_x, clock_button_fallback_y);
        push_motion_and_press(options->scale, launch_back_pt.x, launch_back_pt.y);
      }
      if (frames_run == launch_home_frame + click_release_delta) {
        push_release(options->scale, launch_back_pt.x, launch_back_pt.y);
      }
      if (frames_run == launch_bump_frame) {
        auto* settings_app = static_cast<embark::demo::SettingsApp*>(framework.app(2));
        launch_bright_before = static_cast<int>(settings_app->brightness_percent());
        settings_app->bump_brightness(embark::demo::kBrightStep);
      }
      if (frames_run == launch_quit_frame) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
      }
    } else if (options->eez) {
      // 点 launcher 屏「闹钟」行：Flow SetPage 到 clock 屏 → 桥的屏观察者把这次切屏翻成
      // request_switch("clock")（帧 20 按下 → 帧 21/22 抬起 → 下次 tick 生效）。
      if (frames_run == eez_switch_frame) {
        scroll_into_view(objects.app_row_alarm);  // 同上：先滚进视口
        const PanelPoint row = widget_center(objects.app_row_alarm, launcher_button_fallback_x,
                                             launcher_button_fallback_y);
        push_motion_and_press(options->scale, row.x, row.y);
      }
      if (frames_run == eez_switch_frame + click_release_delta) {
        push_release(options->scale, alarm_row.x, alarm_row.y);
      }
      // 回程：点 clock 屏按钮 → Flow SetPage 回 launcher 屏 →
      // 观察者 request_switch 回启动器（EEZ 屏内按钮承担返回，无导航壳）。
      // 回程点击放帧 70：首次切页动画 22–62 期间 LVGL 屏蔽输入（prev_scr）。
      if (frames_run == eez_home_frame) {
        push_motion_and_press(options->scale, clock_btn.x, clock_btn.y);
      }
      if (frames_run == eez_home_frame + click_release_delta) {
        push_release(options->scale, clock_btn.x, clock_btn.y);
      }
      if (frames_run == eez_quit_frame) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
      }
    } else if (options->click) {
      if (frames_run == press_frame) {
        scroll_into_view(objects.app_row_alarm);
        const PanelPoint row = widget_center(objects.app_row_alarm, launcher_button_fallback_x,
                                             launcher_button_fallback_y);
        push_motion_and_press(options->scale, row.x, row.y);
      }
      // 帧 20：点 launcher 屏「闹钟」行（切到 clock）；帧 70：在 clock 屏点 (X,Y)
      // （默认 = clock 屏按钮中心 → Flow SetPage 回 launcher 屏 → 回启动器；帧 70
      // 落在首次切页动画 22–62 之后，避开输入屏蔽）。
      if (frames_run == press_frame + click_release_delta) {
        push_release(options->scale, alarm_row.x, alarm_row.y);
      }
      if (frames_run == click_second_frame) {
        push_motion_and_press(options->scale, options->click_x, options->click_y);
      }
      if (frames_run == click_second_frame + click_release_delta) {
        push_release(options->scale, options->click_x, options->click_y);
      }
    } else if (options->drag) {
      // 按下 → 每帧推进一个插值点 → 抬起：模拟真手指的连续轨迹。
      if (frames_run == drag_press_frame) {
        push_motion_and_press(options->scale, options->drag_x1, options->drag_y1);
      }
      if (frames_run > drag_press_frame && frames_run < drag_release_frame) {
        const int steps = drag_release_frame - drag_press_frame;
        const int t = frames_run - drag_press_frame;
        push_motion(options->scale,
                    options->drag_x1 + (options->drag_x2 - options->drag_x1) * t / steps,
                    options->drag_y1 + (options->drag_y2 - options->drag_y1) * t / steps);
      }
      if (frames_run == drag_release_frame) {
        push_release(options->scale, options->drag_x2, options->drag_y2);
      }
    } else if (options->ui) {
      // AiDoGUI 验收：帧 10 同步跑完全部检查（无合成输入），帧 30 关窗。
#if defined(EMBARK_AIDOGUI_UI)
      if (frames_run == aidogui_check_frame) {
        aidogui_ok = aidogui_acceptance();
      }
#endif
      if (frames_run == aidogui_quit_frame) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
      }
    } else if (options->ui_nav) {
      // AiDoGUI 双向导航 + 月历翻月：主屏运行时磁贴（第 0 个 = 时钟）→ clock → 返回
      // （设计器事件）→ launcher → 时钟卡（C++ bind_click）→ clock → 翻月键（C++ 回调）。
      // 磁贴不在设计稿里（按注册表运行时建），落点走 launcher_tiles 的观测点 + 兜底坐标。
#if defined(EMBARK_AIDOGUI_UI)
      const PanelPoint tile = aidogui_tile_center(0, uinav_tile_fallback_x, uinav_tile_fallback_y);
      const PanelPoint back =
          aidogui_widget_center("clock", "Btn_Back", uinav_back_fallback_x, uinav_back_fallback_y);
      const PanelPoint card = aidogui_widget_center("launcher", "Card_Clock", uinav_card_fallback_x,
                                                    uinav_card_fallback_y);
      const PanelPoint next_btn =
          aidogui_widget_center("clock", "Btn_Next", uinav_next_fallback_x, uinav_next_fallback_y);
      const PanelPoint prev_btn =
          aidogui_widget_center("clock", "Btn_Prev", uinav_prev_fallback_x, uinav_prev_fallback_y);
      if (frames_run == uinav_tile_frame) {
        push_motion_and_press(options->scale, tile.x, tile.y);
      }
      if (frames_run == uinav_tile_frame + click_release_delta) {
        push_release(options->scale, tile.x, tile.y);
      }
      if (frames_run == uinav_clock_check_frame) {
        uinav_clock_ok = aidogui_clock_check();
      }
      if (frames_run == uinav_back_frame) {
        push_motion_and_press(options->scale, back.x, back.y);
      }
      if (frames_run == uinav_back_frame + click_release_delta) {
        push_release(options->scale, back.x, back.y);
      }
      if (frames_run == uinav_back_check_frame) {
        uinav_back_ok = aidogui_back_check();
      }
      if (frames_run == uinav_card_frame) {
        push_motion_and_press(options->scale, card.x, card.y);
      }
      if (frames_run == uinav_card_frame + click_release_delta) {
        push_release(options->scale, card.x, card.y);
      }
      if (frames_run == uinav_card_check_frame) {
        uinav_card_ok = aidogui_card_check(uinav_base_y, uinav_base_m);
      }
      if (frames_run == uinav_next_frame) {
        push_motion_and_press(options->scale, next_btn.x, next_btn.y);
      }
      if (frames_run == uinav_next_frame + click_release_delta) {
        push_release(options->scale, next_btn.x, next_btn.y);
      }
      if (frames_run == uinav_next_check_frame) {
        uinav_next_ok = aidogui_next_check(uinav_base_y, uinav_base_m);
      }
      if (frames_run == uinav_prev_frame) {
        push_motion_and_press(options->scale, prev_btn.x, prev_btn.y);
      }
      if (frames_run == uinav_prev_frame + click_release_delta) {
        push_release(options->scale, prev_btn.x, prev_btn.y);
      }
      if (frames_run == uinav_prev_check_frame) {
        uinav_prev_ok = aidogui_prev_check(uinav_base_y, uinav_base_m);
      }
#endif
      if (frames_run == uinav_quit_frame) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
      }
    } else if (options->ui_weather) {
      // 天气验收：点主屏天气卡（设计器事件）→ 天气屏对桩 → 点「>」切城市 → 点「返回」→
      // 主屏卡片同源。城市不写死，比的是"屏上的字 == 数据桩里那座城市的字"。
#if defined(EMBARK_AIDOGUI_UI)
      const PanelPoint wcard = aidogui_widget_center(
          "launcher", "Card_Weather", uiweather_card_fallback_x, uiweather_card_fallback_y);
      const PanelPoint wnext = aidogui_widget_center(
          "weather", "Btn_CityNext", uiweather_next_fallback_x, uiweather_next_fallback_y);
      const PanelPoint wback = aidogui_widget_center(
          "weather", "Btn_Back", uiweather_back_fallback_x, uiweather_back_fallback_y);
      if (frames_run == uiweather_card_frame) {
        push_motion_and_press(options->scale, wcard.x, wcard.y);
      }
      if (frames_run == uiweather_card_frame + click_release_delta) {
        push_release(options->scale, wcard.x, wcard.y);
      }
      if (frames_run == uiweather_in_frame) {
        uiweather_in_ok = aidogui_weather_screen_ok(weather->city_index());
      }
      if (frames_run == uiweather_next_frame) {
        push_motion_and_press(options->scale, wnext.x, wnext.y);
      }
      if (frames_run == uiweather_next_frame + click_release_delta) {
        push_release(options->scale, wnext.x, wnext.y);
      }
      if (frames_run == uiweather_next_check_frame) {
        uiweather_next_ok =
            aidogui_weather_screen_ok(weather->city_index()) && weather->city_switches() == 1;
      }
      if (frames_run == uiweather_back_frame) {
        push_motion_and_press(options->scale, wback.x, wback.y);
      }
      if (frames_run == uiweather_back_frame + click_release_delta) {
        push_release(options->scale, wback.x, wback.y);
      }
      if (frames_run == uiweather_card_check_frame) {
        uiweather_card_ok = aidogui_weather_card_ok(weather->city_index());
      }
#endif
      if (frames_run == uiweather_quit_frame) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
      }
    } else if (options->ui_launcher_pages) {
      // 主屏分页验收：磁贴/页/页码点都是运行时按注册表建的 → 横向划到第 2 页 → 点只存在于
      // 第 2 页的「相册」→ 相册屏点「返回」回主屏 → 点第 1 页的「设置」磁贴进设置屏 →
      // 点「亮度 +1」验收逻辑动作绑定 → 设置屏点「返回」回主屏。回程一律靠屏上的按钮
      //（上滑返回手势已去掉）。落点走"读真实几何 + 兜底"，以后挪设计稿也不会瞎点。
#if defined(EMBARK_AIDOGUI_UI)
      auto* ulp_settings_app = static_cast<embark::demo::SettingsApp*>(framework.app(2));
      if (frames_run == ulp_check_frame) {
        ulp_tiles_ok = aidogui_launcher_pages_check(framework.apps());
      }
      if (frames_run == ulp_drag_press_frame) {
        push_motion_and_press(options->scale, ulp_drag_x_from, ulp_band_y);
      }
      if (frames_run > ulp_drag_press_frame && frames_run < ulp_drag_release_frame) {
        const int steps = ulp_drag_release_frame - ulp_drag_press_frame;
        const int t = frames_run - ulp_drag_press_frame;
        push_motion(options->scale, ulp_drag_x_from + (ulp_drag_x_to - ulp_drag_x_from) * t / steps,
                    ulp_band_y);
      }
      if (frames_run == ulp_drag_release_frame) {
        push_release(options->scale, ulp_drag_x_to, ulp_band_y);
      }
      if (frames_run == ulp_page_check_frame) {
        ulp_page_ok = aidogui_launcher_page_check();
      }
      if (frames_run == ulp_tile_click_frame) {
        const PanelPoint album_tile =
            aidogui_tile_center(4, ulp_tile_fallback_x, ulp_tile_fallback_y);
        ulp_tile_x = album_tile.x;
        ulp_tile_y = album_tile.y;
        push_motion_and_press(options->scale, ulp_tile_x, ulp_tile_y);
      }
      if (frames_run == ulp_tile_click_frame + click_release_delta) {
        push_release(options->scale, ulp_tile_x, ulp_tile_y);
      }
      if (frames_run == ulp_album_check_frame) {
        ulp_album_ok = aidogui_launcher_album_check();
      }
      // 相册屏「返回」→ 回主屏（组件事件 embark_ui_open_launcher）。
      if (frames_run == ulp_album_back_frame) {
        const PanelPoint back =
            aidogui_widget_center("album", "Btn_Back", ulp_back_fallback_x, ulp_back_fallback_y);
        ulp_back_x = back.x;
        ulp_back_y = back.y;
        push_motion_and_press(options->scale, ulp_back_x, ulp_back_y);
      }
      if (frames_run == ulp_album_back_frame + click_release_delta) {
        push_release(options->scale, ulp_back_x, ulp_back_y);
      }
      if (frames_run == ulp_back_check_frame) {
        ulp_back_ok = aidogui_launcher_back_check(framework.apps());
      }
      // 留存断言过了（回来仍在第 2 页），验收脚本自己把页翻回第 1 页 —— 反向拖一遍。
      if (frames_run == ulp_back_drag_press_frame) {
        push_motion_and_press(options->scale, ulp_drag_x_to, ulp_band_y);
      }
      if (frames_run > ulp_back_drag_press_frame && frames_run < ulp_back_drag_release_frame) {
        const int steps = ulp_back_drag_release_frame - ulp_back_drag_press_frame;
        const int t = frames_run - ulp_back_drag_press_frame;
        push_motion(options->scale, ulp_drag_x_to + (ulp_drag_x_from - ulp_drag_x_to) * t / steps,
                    ulp_band_y);
      }
      if (frames_run == ulp_back_drag_release_frame) {
        push_release(options->scale, ulp_drag_x_from, ulp_band_y);
      }
      if (frames_run == ulp_page1_check_frame) {
        ulp_page1_ok = aidogui_launcher_page1_check();
      }
      // 回到第 1 页之后，第 2 格就是「设置」。
      if (frames_run == ulp_settings_tile_frame) {
        const PanelPoint tile =
            aidogui_tile_center(1, ulp_settings_tile_fallback_x, ulp_settings_tile_fallback_y);
        ulp_tile_x = tile.x;
        ulp_tile_y = tile.y;
        push_motion_and_press(options->scale, ulp_tile_x, ulp_tile_y);
      }
      if (frames_run == ulp_settings_tile_frame + click_release_delta) {
        push_release(options->scale, ulp_tile_x, ulp_tile_y);
      }
      if (frames_run == ulp_settings_check_frame) {
        ulp_settings_ok = aidogui_launcher_settings_check();
        if (ulp_settings_app != nullptr) {
          // 亮度存在 KV 里、会跨进程留下来（真机上这是特性，验收里它是脏起点）：先把亮度
          // 拨到 50%，让「点一下 +10」有确定起点，也不会把屏压到全黑。
          ulp_settings_app->bump_brightness(
              50 - static_cast<int>(ulp_settings_app->brightness_percent()));
          ulp_bright_before = static_cast<int>(ulp_settings_app->brightness_percent());
          ulp_panel_ok = aidogui_settings_panel_check(ulp_settings_app->brightness_percent());
        }
      }
      // 设置屏正文（Body）上拖。正文 422 px、视口 276 px —— 但"能滚"不只是容器标了
      // SCROLLABLE 就行：LVGL 的滚动链是从"被按到的控件"往上找滚动容器的
      // （third_party/lvgl/src/core/lv_indev_scroll.c 的 find_scroll_obj），而
      // lv_obj_pos.c:955 的 lv_obj_hit_test 第一行就要求 CLICKABLE —— 手指落在卡片的
      // 空白处（卡片、标签都不可点）时 act_obj 直接是屏本身，滚动链没有起点，
      // 容器再 scrollable 也不会动（真机"拖不动"就是这么来的）。所以 Body 自己必须
      // CLICKABLE：今天 .aid 里 Body 的 flags.clickable = true，导出即
      // lv_obj_add_flag(ui_settings_Body, LV_OBJ_FLAG_CLICKABLE)。
      if (frames_run == ulp_scroll_press_frame) {
        if (lv_obj_t* body = embark::demo::aidogui_ui_bridge_widget("settings", "Body")) {
          ulp_scroll_before = lv_obj_get_scroll_y(body);
        }
        push_motion_and_press(options->scale, ulp_scroll_x, ulp_scroll_y_from);
      }
      if (frames_run > ulp_scroll_press_frame && frames_run < ulp_scroll_release_frame) {
        const int steps = ulp_scroll_release_frame - ulp_scroll_press_frame;
        const int t = frames_run - ulp_scroll_press_frame;
        push_motion(options->scale, ulp_scroll_x,
                    ulp_scroll_y_from + (ulp_scroll_y_to - ulp_scroll_y_from) * t / steps);
      }
      if (frames_run == ulp_scroll_release_frame) {
        push_release(options->scale, ulp_scroll_x, ulp_scroll_y_to);
      }
      if (frames_run == ulp_scroll_check_frame) {
        if (lv_obj_t* body = embark::demo::aidogui_ui_bridge_widget("settings", "Body")) {
          ulp_scroll_after = lv_obj_get_scroll_y(body);
        }
        ulp_scroll_ok = aidogui_settings_scroll_check(ulp_scroll_before, ulp_scroll_after);
      }
      // 设置屏「亮度 ＋」：屏内按钮，事件由 App 自己在 repaint 钩子里挂（不进导航绑定表）。
      if (frames_run == ulp_bright_frame) {
        const PanelPoint bright = aidogui_widget_center(
            "settings", "Btn_Plus", ulp_bright_fallback_x, ulp_bright_fallback_y);
        ulp_bright_x = bright.x;
        ulp_bright_y = bright.y;
        push_motion_and_press(options->scale, ulp_bright_x, ulp_bright_y);
      }
      if (frames_run == ulp_bright_frame + click_release_delta) {
        push_release(options->scale, ulp_bright_x, ulp_bright_y);
      }
      if (frames_run == ulp_bright_check_frame) {
        const bool moved = ulp_settings_app != nullptr && ulp_bright_before >= 0 &&
                           aidogui_launcher_bright_check(
                               static_cast<std::size_t>(ulp_bright_before),
                               ulp_settings_app->brightness_percent());
        const bool clamped = aidogui_launcher_bright_clamp_check();
        ulp_bright_ok = moved && clamped;
      }
      if (frames_run == ulp_settings_back_frame) {
        const PanelPoint back =
            aidogui_widget_center("settings", "Btn_Back", ulp_back_fallback_x, ulp_back_fallback_y);
        ulp_back_x = back.x;
        ulp_back_y = back.y;
        push_motion_and_press(options->scale, ulp_back_x, ulp_back_y);
      }
      if (frames_run == ulp_settings_back_frame + click_release_delta) {
        push_release(options->scale, ulp_back_x, ulp_back_y);
      }
      if (frames_run == ulp_home_check_frame) {
        ulp_home_ok = aidogui_launcher_home_check(framework.apps());
      }
#endif
      if (frames_run == ulp_quit_frame) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
      }
    }
    if (options->shot_path != nullptr && frames_run == options->shot_frame) {
      if (!save_screenshot(display, options->shot_path)) {
        hp::exit_process(1);
      }
    }
    if (options->quit_at > 0 && frames_run == options->quit_at) {
      SDL_Event quit{};
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
    }

    if (frames_limit > 0 && frames_run >= frames_limit) {
      if (options->screenshot != nullptr && !save_screenshot(display, options->screenshot)) {
        hp::exit_process(1);
      }
      break;
    }
    hp::ui_loop_delay(static_cast<std::uint32_t>(options->delay_ms));
  }

  // --- 观测与断言 -------------------------------------------------------------
  const auto* launcher = static_cast<const embark::demo::LauncherApp*>(framework.app(0));
  const auto* clock = static_cast<const embark::demo::ClockApp*>(framework.app(1));
  auto* settings = static_cast<embark::demo::SettingsApp*>(framework.app(2));

  ELOG_INFO("显示与前台：{} 帧；前台 {}；switches={}", frames_run,
            framework.apps().at(framework.foreground())->name(), framework.switches());
  ELOG_INFO("启动器（屏名 launcher，界面来源见启动日志）：enters={} resumes={} 前台帧={}",
            launcher->enters(), launcher->resumes(), launcher->foreground_ticks());
  ELOG_INFO(
      "时钟（屏名 clock）：前台帧={} 亮度={}%（设置页镜像={}%）；LVGL 未回收 {} 字节（不申请后台，"
      "时间只在进前台时推）",
      clock->foreground_ticks(), clock->brightness(), settings->brightness_percent(),
      embark_lvgl_outstanding_bytes());
  ELOG_INFO("天气（屏名 weather）：城市 {}（下标 {}）切城 {} 次；enters={} resumes={} 前台帧={}",
            weather->city_name(), weather->city_index(), weather->city_switches(),
            weather->enters(), weather->resumes(), weather->foreground_ticks());

  int exit_code = 0;
  if (options->launch) {
    // 界面已是 AiDoGUI：观测也走 AiDoGUI 的账（EEZ 的 last_screen/current_screen 在本配置下
    // 永远是空/未初始化，写进断言只会变成永远为假的死条件）。
    const char* last_screen = embark::demo::aidogui_ui_nav_last_screen();
    const char* current_screen = embark::demo::aidogui_ui_bridge_current_screen();
    const bool launched_ok =
        framework.switches() == 4 && launcher->enters() == 1 && launcher->resumes() == 2 &&
        launcher->foreground_ticks() > 0 && clock->enters() == 1 && clock->resumes() == 1 &&
        clock->foreground_ticks() > 0 &&
        // 亮度从「本进程 onCreate 读到的值」+10（钳位）—— 起点跨进程不固定，不写死 70。
        clock->brightness() == embark::demo::clamp_percent(launch_bright_before +
                                                           embark::demo::kBrightStep) &&
        // 3 次点击各自登记一次导航请求（设计器事件走 request_app，钳在 click_hits 上；
        // switch_requests 记的是「屏被换掉后按屏名切 App」，这条故事不经过那条路）。
        embark::demo::aidogui_ui_nav_click_hits() == 3 && last_screen != nullptr &&
        std::strcmp(last_screen, "launcher") == 0 && current_screen != nullptr &&
        std::strcmp(current_screen, "launcher") == 0 && framework.foreground() == 0;
    ELOG_INFO(
        "launch 明细：switches={} launcher {}/{} clock {}/{} 亮度 {}%（起点 {}%）；导航命中 {}；"
        "最后屏 {} 当前屏 {} 前台 {}",
        framework.switches(), launcher->enters(), launcher->resumes(), clock->enters(),
        clock->resumes(), clock->brightness(), launch_bright_before,
        embark::demo::aidogui_ui_nav_click_hits(), last_screen != nullptr ? last_screen : "<null>",
        current_screen != nullptr ? current_screen : "<null>", framework.foreground());
    if (launched_ok) {
      ELOG_INFO("〔launch 验收通过〕");
    } else {
      ELOG_INFO("〔launch 验收失败〕");
    }
    if (!launched_ok) {
      exit_code = 2;
    }
  }
  if (options->click) {
    // 点 launcher 屏按钮 → 切 clock（EEZ 驱动）→ 在 clock 屏点 (X,Y)（默认回启动器）。
    // 点击落点随前台切换走：同一套坐标只对当前屏的按钮有效，这正是薄壳切换的验收点。
    const char* last_screen = embark::demo::eez_ui_nav_last_screen();
    const bool clicked_ok =
        framework.switches() == 2 && launcher->enters() == 1 && launcher->resumes() == 1 &&
        clock->enters() == 1 && clock->resumes() == 0 && settings->enters() == 0 &&
        embark::demo::eez_ui_nav_switch_requests() == 2 && last_screen != nullptr &&
        std::strcmp(last_screen, "launcher") == 0 &&
        embark::demo::eez_ui_bridge_current_screen() == 1 && framework.foreground() == 0;
    if (clicked_ok) {
      ELOG_INFO("〔click 验收通过〕");
    } else {
      ELOG_INFO("〔click 验收失败〕");
    }
    if (!clicked_ok) {
      exit_code = 2;
    }
  }
  if (options->drag) {
    // EEZ 屏上没有可拖动的槽位了：拖动只走输入通路，不误触按钮、不切 App。
    const bool dragged_ok =
        framework.switches() == 0 && launcher->enters() == 1 && framework.foreground() == 0;
    ELOG_INFO("〔drag 验收{}〕：拖动 {}px，前台 {}，switches={}", dragged_ok ? "通过" : "失败",
              options->drag_y2 - options->drag_y1,
              framework.apps().at(framework.foreground())->name(), framework.switches());
    if (!dragged_ok) {
      exit_code = 2;
    }
  }
  if (options->ui) {
    // --ui 的检查在帧 10 就断完了（aidogui_acceptance 自己打每条证据行），这里只落结论。
    ELOG_INFO("〔aidogui 验收{}〕", aidogui_ok ? "通过" : "失败");
    if (!aidogui_ok) {
      exit_code = 2;
    }
  }
  if (options->ui_nav) {
    // --ui-nav 的五段检查在帧 22/40/62/80/96 就断完了（各自打了证据行），这里只落结论。
    const bool uinav_ok =
        uinav_clock_ok && uinav_back_ok && uinav_card_ok && uinav_next_ok && uinav_prev_ok;
    ELOG_INFO("〔aidogui 导航验收{}〕（前台 {}，switches={}，导航命中 {} 次，月历基准 {}-{}）",
              uinav_ok ? "通过" : "失败", framework.apps().at(framework.foreground())->name(),
              framework.switches(), embark::demo::aidogui_ui_nav_click_hits(), uinav_base_y,
              uinav_base_m);
    if (!uinav_ok) {
      exit_code = 2;
    }
  }
  if (options->ui_weather) {
    // --ui-weather 的三段检查在帧 22/40/58 就断完了（各自打了证据行），这里只落结论。
    const bool uiweather_ok = uiweather_in_ok && uiweather_next_ok && uiweather_card_ok;
    ELOG_INFO("〔aidogui 天气验收{}〕（前台 {}，switches={}，切城 {} 次，导航命中 {} 次，城市 {}）",
              uiweather_ok ? "通过" : "失败", framework.apps().at(framework.foreground())->name(),
              framework.switches(), weather->city_switches(),
              embark::demo::aidogui_ui_nav_click_hits(), weather->city_name());
    if (!uiweather_ok) {
      exit_code = 2;
    }
  }
  if (options->ui_launcher_pages) {
    // 八段检查在帧 10/44/64/78/108/122/134/150 就断完了（各自打了证据行），这里只落结论，
    // 外加两条全局不变量（ADR 0015）：① 没有任何 App 申请后台体；② 写控件值的屏都声明了重推。
    const bool ulp_ok = ulp_tiles_ok && ulp_page_ok && ulp_album_ok && ulp_back_ok &&
                        ulp_page1_ok && ulp_settings_ok && ulp_panel_ok && ulp_scroll_ok &&
                        ulp_bright_ok && ulp_home_ok && framework.foreground() == 0 &&
                        framework.switches() == 4;
    bool no_background_ok = true;
    for (embark::AppId i = 0; i < framework.apps().size(); ++i) {
      const auto* app = framework.apps().at(i);
      const bool suspended = app->settings().background == embark::BackgroundPolicy::suspend;
      ELOG_INFO("      槽 {} {} 的后台策略 = {}", i, app->name(),
                suspended ? "suspend（没有后台体）" : "有后台体 ← 不该有");
      if (!suspended) {
        no_background_ok = false;
      }
    }
    bool repaint_ok = true;
#if defined(EMBARK_AIDOGUI_UI)
    const int repaint_bindings = embark::demo::aidogui_ui_nav_repaint_bindings();
    const int repaint_calls = embark::demo::aidogui_ui_nav_repaint_calls();
    // 下限断言，不是等号：每加一个"进屏要重推"的 App，这个数就该 +1。
    // 用 == 3 会让"新加 App 绑了 repaint"变成验收失败 —— 那就等于把新 App 拦在门外了。
    constexpr int kMinimumRepaintBindings = 3;  // launcher / clock / weather 三个老账
    repaint_ok = repaint_bindings >= kMinimumRepaintBindings && repaint_calls >= kMinimumRepaintBindings;
    ELOG_INFO(
        "值重推：声明 {} 个屏（launcher / clock / weather 为老账，往后的 App 只会更多；"
        "期望 ≥{}），屏装载后被调 {} 次（期望 ≥{}）",
        repaint_bindings, kMinimumRepaintBindings, repaint_calls, kMinimumRepaintBindings);
#endif
    const bool ulp_all_ok = ulp_ok && no_background_ok && repaint_ok;
    ELOG_INFO(
        "〔主屏分页验收{}〕（磁贴 {} / 页 {} / 点 {}；设置屏 {} / 正文滚动 {} px；"
        "亮度百分比 {}%→{}%；前台 {}，switches={}；无后台体 = {}，值重推 = {}）",
        ulp_all_ok ? "通过" : "失败", embark::demo::launcher_tiles_count(),
        embark::demo::launcher_tiles_page_count(), embark::demo::launcher_tiles_dot_count(),
        ulp_settings_ok ? "已进" : "未进",
        ulp_scroll_after < 0 ? -ulp_scroll_after : ulp_scroll_after, ulp_bright_before,
        settings->brightness_percent(),
        framework.apps().at(framework.foreground())->name(), framework.switches(),
        no_background_ok ? "是" : "否", repaint_ok ? "对" : "错");
    if (!ulp_all_ok) {
      exit_code = 2;
    }
  }
  if (options->eez) {
    // 故事线：点 launcher 屏按钮 → Flow SetPage 到 clock 屏 → 桥的屏观察者 →
    // request_switch("clock") → ClockApp 前台（switches 1）→ 点 clock 屏按钮回启动器（switches 2）。
    // 这就是“EEZ 里切屏 = 切前台 App”（屏名约定 == App 名，见 eez_ui_nav.h）。
    // 屏表（构建期从 EEZ 生成代码解析）自检：表非空，且每项都能名字→id 反查回来
    // （未来按约定加屏，这里随之增长，不需要改本文件）。
    const int screen_count = embark::demo::eez_ui_bridge_screen_count();
    bool screen_table_ok = screen_count > 0;
    for (int i = 0; i < screen_count; ++i) {
      const char* screen_name = embark::demo::eez_ui_bridge_screen_name(i);
      ELOG_INFO("eez 屏表[{}]：{}", i, screen_name != nullptr ? screen_name : "<null>");
      if (screen_name == nullptr || embark::demo::eez_ui_bridge_screen_id(screen_name) < 0) {
        screen_table_ok = false;
      }
    }
    // 变量表（构建期从 eez-flow.h 的 FlowGlobalVariables 解析）自检：表非空、按名可查、
    // 未命中返回 kEezVarNone；再对 launcher_tap_count 做 int/float/bool 三型读写往返
    // （变量桥按名读写 == UI 显示数据的接口，见 eez_ui_bridge.h「四」）。
    const int var_count = embark::demo::eez_ui_bridge_var_count();
    bool var_table_ok = var_count > 0;
    for (int i = 0; i < var_count; ++i) {
      const char* var_name = embark::demo::eez_ui_bridge_var_name(i);
      ELOG_INFO("eez 变量表[{}]：{}", i, var_name != nullptr ? var_name : "<null>");
      if (var_name == nullptr || embark::demo::eez_ui_bridge_var_index(var_name) < 0) {
        var_table_ok = false;
      }
    }
    bool var_roundtrip_ok = true;
    if (embark::demo::eez_ui_bridge_var_index("launcher_tap_count") == 0) {
      std::int32_t got_int = 0;
      float got_float = 0.0f;
      bool got_bool = false;
      const bool set_int = embark::demo::eez_ui_bridge_set_var_int("launcher_tap_count", 42);
      const bool get_int = embark::demo::eez_ui_bridge_get_var_int("launcher_tap_count", got_int);
      const bool set_float = embark::demo::eez_ui_bridge_set_var_float("launcher_tap_count", 3.5f);
      const bool get_float =
          embark::demo::eez_ui_bridge_get_var_float("launcher_tap_count", got_float);
      const bool set_bool = embark::demo::eez_ui_bridge_set_var_bool("launcher_tap_count", true);
      const bool get_bool =
          embark::demo::eez_ui_bridge_get_var_bool("launcher_tap_count", got_bool);
      var_roundtrip_ok = set_int && get_int && got_int == 42 && set_float && get_float &&
                         got_float == 3.5f && set_bool && get_bool && got_bool;
      ELOG_INFO("变量读写往返：int {} -> {}（{}），float {} -> {}（{}），bool true -> {}（{}）",
                set_int ? "set" : "fail", got_int, get_int ? "get" : "fail",
                set_float ? "set" : "fail", got_float, get_float ? "get" : "fail",
                set_bool ? "set" : "fail", got_bool, get_bool ? "get" : "fail");
    } else {
      var_roundtrip_ok = false;
    }
    const bool no_such_var_ok =
        embark::demo::eez_ui_bridge_var_index("no_such_var") == embark::demo::kEezVarNone;
    if (!no_such_var_ok) {
      var_roundtrip_ok = false;
    }
    const char* last_screen = embark::demo::eez_ui_nav_last_screen();
    const bool eez_ok =
        framework.switches() == 2 && launcher->enters() == 1 && launcher->resumes() == 1 &&
        launcher->foreground_ticks() > 0 && clock->enters() == 1 && clock->resumes() == 0 &&
        embark::demo::eez_ui_nav_switch_requests() == 2 && last_screen != nullptr &&
        std::strcmp(last_screen, "launcher") == 0 &&
        embark::demo::eez_ui_bridge_current_screen() == 1 && framework.foreground() == 0 &&
        screen_table_ok && var_table_ok && var_roundtrip_ok && no_such_var_ok;
    ELOG_INFO(
        "eez: switches={} enters={} resumes={} ticks={} current_screen={} screens={} vars={} "
        "nav_requests={} last_screen={}",
        framework.switches(), launcher->enters(), launcher->resumes(), launcher->foreground_ticks(),
        embark::demo::eez_ui_bridge_current_screen(), screen_count, var_count,
        embark::demo::eez_ui_nav_switch_requests(),
        last_screen != nullptr ? last_screen : "<none>");
    if (eez_ok) {
      ELOG_INFO("〔eez 验收通过〕");
    } else if (kAidoguiOwnsAppLayer) {
      // AiDoGUI 已接管 App 层（本构建定义了 EMBARK_AIDOGUI_UI）：App 只会 attach aidogui
      // 的 nav，EEZ 屏也不再被装载，故切屏断言不成立 —— 这不是回归（ADR 0011）。
      ELOG_INFO(
          "〔eez 验收跳过〕AiDoGUI 已接管 App 层，EEZ nav 不再被 App attach；老 EEZ 屏未装载。");
    } else {
      ELOG_INFO("〔eez 验收失败〕");
      exit_code = 2;
    }
  }

  framework.shutdown();
  hp::exit_process(exit_code);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  Options options = parse_options(argc, argv);

  ELOG_INFO("UI 任务启动：栈 {} 字（{} KB），优先级 {}，周期 {} ms", embark::ui_task_stack_words,
            embark::ui_task_stack_words * static_cast<int>(sizeof(StackType_t)) / 1024,
            static_cast<int>(embark::ui_task_priority), embark::ui_loop_period_ms);

  const Error task_error = hp::start_ui_task(&ui_main, &options);
  if (task_error != Error::none) {
    char text[24];
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::error_text(text, task_error));
    return 1;
  }

  hp::start_scheduler();
  return 0;
}
