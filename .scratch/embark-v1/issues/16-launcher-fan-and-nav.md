# 16 · 启动器：扇形半环 + 导航壳 + App 元数据

Status: resolved
Type: feature
Blocked by: —
来源: /grill-with-docs 多轮设计问答（用户指定参考样式 `06-扇形半环.html`，"风格得换"；
管理边界 a+b、导航 a+c、状态行"去除 tick 计数"均出自用户原话）

## 目标

启动器 App 管理全部 App：扇形半环列出、点击切换前台、主屏与框架导航壳（统一返回 + 状态行）
由框架提供；App 元数据（中文标题/图标/主题色）编译期进注册表。SettingsApp 硬编码的
"Back to clock" 移除，返回统一走框架。

## 已定契约（设计已敲定，实现按此）

### 扇形半环（骨架照搬参考 HTML，风格换深色科技风）
- 几何：240×320 竖屏，支点 `(120, 270)`（底部中心），弧半径 R=82，槽距 STEP=26°，弧关于
  竖直轴对称，选中槽停正上方；最多 `max_apps` 槽（超出编译期报错）。
- 视觉：背景近黑蓝；虚线弧轨（dash 2px/6px）+ 实线选中弧段；每槽 = 圆形细描边（线宽 2px）+
  图标（LV_SYMBOL 码点铺底，无图标用中文首字兜底）；选中槽反色：强调色填充圆 + 深色图标 +
  高亮标题；全部走设计令牌（下表），禁止魔法颜色。
- 交互：拖动 1px≈0.1 槽（dy），滚轮/按键 ±1 槽；弹簧缓动 `pos += (tgt-pos)*0.15` 在 5 ms
  UI 节拍推进；点击选中槽 = `request_switch` 启动；点击非选中槽只转正。
- 文本：槽标题 = App 元数据中文标题，字体 = 静态子集字库（issue 17）。

### 框架导航壳（App 无感知）
- `request_home()`：`request_switch(registry[0])` 的语义别名；注册表首位即主屏（启动器）。
- 统一返回键：非主屏 App 前台时，壳左上渲染返回箭头（LV_SYMBOL_LEFT），点击等效
  `request_home()`；主屏前台不渲染。
- 壳用 `lv_layer_top` 实现，跨 `lv_scr_load` 持久；App 契约不改（仍自建全屏 screen、自行
  `lv_scr_load`）。
- 状态行：壳顶部，左侧时间（宿主墙钟 HH:MM；真机 RTC 未接 → 占位），右侧当前前台 App 的后台
  策略名（`悬` / `tick:100ms` / `own:50ms`）；**不渲染节拍计数**（用户明确）。
- 状态行/返回键文本在静态子集 & 缺字审计范围内。

### App 元数据（零堆、编译期）
- 注册表条目扩展：中文 title、icon（LV_SYMBOL 码点，可选）、accent 主题色（可选，默认取全局
  强调色）；随 `EMBARK_APP_TABLE` 展开为 constexpr 表，表长与 App 数 static_assert 一致。
- 现有 4 个 demo App + 启动器 = 5 条；注册顺序：启动器首位。

### 设计令牌（深色科技风，首版）
| 令牌 | 值 | 用途 |
|---|---|---|
| bg | `#0e141b` | 全局底色 |
| panel | `#16212e` | 面板/按钮底 |
| text_primary | `#e8eef7` | 标题、正文 |
| text_secondary | `#8fa0b5` | 次级说明 |
| accent | `#39d0c4` | 选中弧、强调、返回键 |
| rail | `#2b3a4d` | 虚线弧轨 |
| ring | `#41536b` | 未选中圆描边 |
| radius | 8px | 面板圆角 |
| status_h | 28px | 状态行高 |

## 验收（草案）
- 宿主：扇形渲染截图（`--screenshot`）；`--click` 点选中槽 → 切到目标 App；点返回键 →
  回启动器；拖动/滚轮换位可观测（新增 `--drag x1,y1,x2,y2` 合成拖动或日志断言目标槽变化）。
- 单元：弹簧收敛（给定初始偏差，有限步内 |pos-tgt|<0.01）；元数据表与 App 表长度一致；
  启动器槽位函数（角度↔槽号、wrap）边界。
- 契约：SettingsApp 无自绘返回；切换路径只走 `request_switch` 与 `request_home`。
- 零分配审计用例仍全绿（元数据 constexpr、壳对象静态持有）。

## 备注
- spec 正文 v1 定稿不动，落地时再并入 v1.x 增补。
- 与 issue 17 依赖：中文标题进静态子集字库并过缺字审计；返回值图标的 LV_SYMBOL 码点
  是内置字形，不进字库范围。
- demo App 元数据示例（实现时定）：clock→LV_SYMBOL_REFRESH 等，无 icon 者首字兜底。

## Answer（2026-10-05，提交 `ca42107`）

已完成：启动器扇形半环 + 框架导航壳 + App 元数据，全部契约项落地。

### 交付物
| 件 | 位置 | 说明 |
|---|---|---|
| 扇形几何（纯数学，可单测） | include/embark/launcher_geometry.h | 支点 (120,270)、R=82、STEP=26°、拖动/弹簧/命中/UTF-8 首字，不碰 LVGL |
| 设计令牌 | include/embark/design_tokens.h | 深色科技风 9 令牌，LVGL 侧唯一取色来源 |
| 启动器 App | app/launcher_app.{h,cpp} | 虚线弧轨 + 选中弧段 + 槽圆/图标/标题；拖动/点按/按键；5ms 弹簧 |
| 框架导航壳 | platform/common/nav_shell.{h,cpp} | lv_layer_top 状态行（墙钟 + 策略名）+ 返回键；跨 lv_scr_load 持久 |
| 框架接线 | include/embark/{app,framework,ui_port}.h、src/embark/framework.cpp | request_home()、notify_foreground()、take_home_request 循环边界消费、title/icon/accent 虚函数 |
| 端口接线 | platform/common/lvgl_ui_port.{h,cpp} | NavShell 成员 + 三个转发 |
| 静态子集字库 | assets/fonts/embark_zh_14.c + tools/font/ | 114 字形（ASCII + 13 汉字 + 5 FA），压缩 bpp4；gen_font.mjs 增 RLE 忠实解码验证与 alpha 提升 |
| 宿主验收 | platform/host/ui_demo.cpp、ui_tour.cpp | --launch / --click [x,y] / --drag x1,y1,x2,y2 / --own-task；tour 6 App |
| 单元测试 | tests/kernel/test_launcher_geometry.cpp | 弹簧收敛/单步、槽位偏移/中心、命中判定、drag_target、元数据契约 7 例 |

### 验收对照
- 宿主截图 + 像素采样核验（Pillow）：选中槽圆形反色、图标/标题实心、环/轨/背景全部命中令牌色。
- `embark_host_ui.exe --launch`：switches==3（launcher→clock→settings→返回键→launcher），
  返回键收起、home_requests==1；`--drag 120,220 120,205`：dy=-10 → 目标槽 1；
  `--own-task`：ticker 13 条回流。全部退出码 0。
- `embark_host_tour.exe`：119 帧、14 项自检全过（switches==3、launcher 1/1、home_requests==1…）。
- 单元：108 用例全绿（新增 7 例）；零分配审计保持全绿。
- 契约：SettingsApp 已移除自绘 "Back to clock"（on_back 与按钮一并删除），切换只走
  request_switch / request_home；demo App 标题中文、"无 icon 者首字兜底"按契约生效。

### 备注
- push 尚未执行（需用户同意）；真机 RTC 未接，状态行时间 = 宿主 epoch+8h（契约占位）。
- 字体：lv_font_conv 1.5.3 小字号 CJK 细笔画偏淡（最大 alpha ~13/15），gen_font.mjs 用
  gain 2.4 提升（Noto OFL 可改）；LV_USE_FONT_COMPRESSED 已开（压缩字形必需）。
- 验收文档：acceptance-v1.md 清单更新记录第 7 条、README / common-pitfalls /
  messages-and-background 已同步。
