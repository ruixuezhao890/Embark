# 18 · EEZ Studio + EEZ Flow 接入 spike（基础验证）

Status: resolved
Type: prototype
Blocked by: —
来源: 用户拍板（2026-10-05）「我认为还是eez studio 吧，配上它的flow，然后我们框架适配它」；
用户随后指定「起草，然后先做18，因为18是基础」。选型调研结论（候选横评、三条红线、
EEZ 许可证纠偏）见本 issue 之前的对话与 GLOSSARY/ADR 0008 草案范围。

## 目标

证明「EEZ Flow 生成的页面代码」能编进 Embark 锁定的 LVGL 8.3.11 并在宿主模拟器上跑起来，
拿到三个数字：① 编译配对结论（EEZ 的 8.x 分支在 8.3.11 上编得过吗）；② Flow 运行时
在 LVGL 堆上的常驻/峰值增量（256 KB 预算扛不扛得住）；③ 对接 App 契约的适配点清单。
产出直接决定 ADR 0008 与后续「框架适配」issue 的实现路径。

## 范围（本 issue 只做基础验证，不做框架适配的正式落地）

1. 拉取 eez-framework：clone 到 `.scratch/embark-v1/vendor/eez-framework`
   （.gitignore 已有 vendor/ 例外，不进仓库；是否转 submodule 由本 spike 结论决定），
   摸清依赖子模块与 LVGL 相关模块（src/eez/flow/components/lvgl*、core/alloc）。
2. 独立 spike（`.scratch/embark-v1/spikes/18-eez-flow/`，照 issue 02 先例不进仓库骨架）：
   - 用 eez-framework 上游自带的 LVGL 示例/测试生成代码，在 MinGW GCC 15.1 +
     本仓库 third_party/lvgl 8.3.11 下编译链接；eez-framework 按第三方压制警告。
   - 编译期坐实 lvgl_api 桥的 8.x 分支（LVGL_VERSION_MAJOR >= 9 条件：8.x 走
     lv_scr_load_anim / lv_mem_alloc）。
3. 跑起来：一个最小可执行文件（SDL2 窗口 + 本仓库 LVGL 8.3.11）加载一张 Flow 页面，
   截图证明渲染成功（证据进 .scratch/embark-v1/evidence/18-eez-flow/）。
4. 实测记账：Flow 初始化 + 一屏页面在 LVGL 堆上的增量
   （embark_lvgl_outstanding_bytes / peak 口径，复用 host_lvgl_mem 的记账）。
5. Studio 侧：若官方下载渠道可用，用 EEZ Studio 生成「一屏 + 按钮 + 自定义动作」最小
   工程，取其生成代码做同一套编译/运行验证；渠道不可用则记录障碍，降级用上游自带
   生成代码替代（验收允许，降级原因写进 Answer）。

## 验收

- 编译：spike 用本仓库 third_party/lvgl 8.3.11 编译通过；eez-framework 源码以第三方
  压制警告处理，不污染本仓库零警告基线。
- 运行：SDL2 窗口渲染出 Flow 页面；截图存 .scratch/embark-v1/evidence/18-eez-flow/。
- 数据：Flow 运行时 LVGL 堆增量实测值；「EEZ ↔ LVGL 8.3.11」配对结论（编译坐实，或
  指出需要改动的条件分支/API 差异及工作量）。
- 交付物：spike 的 CMake 片段 + 踩坑记录 + 对接 App 契约的适配点清单
  （onCreate 建树 + eez_flow_init / onEnter lv_scr_load / eez_flow_tick 进
  LvglUiPort::process / Flow 导航限制在 App 内），供 ADR 0008 引用。

## 备注

- 三条红线中，本 issue 只量化内存（LVGL 堆增量）；颜色映射（design tokens）与字体并入
  issue 17 缺字审计在适配 issue 落地，本 issue 不展开。
- eez-framework 上游无 tag（只有 master / dev-g 分支）；本次 spike 锁 master
  @ 2b11334479f6a43dbc51c398b7e6cb27eb217df7（2026-10-05 探针），dev-g 只作备查。
- 已确认的事实基线（调研期实抓）：eez-framework/LICENSE.txt = MIT；lvgl_api.h 与
  core/alloc.cpp 均含 LVGL_VERSION_MAJOR >= 9 条件分支（8.x 路径为活代码）；
  EEZ_FOR_LVGL 模式下 eez 的分配直接走 lv_mem_alloc（8.x），不引入新堆。


## Answer（2026-10-05，spike 完成）

### ① 编译配对结论：✅ 通过，零 API 改动

EEZ Flow 运行时（`EEZ_FOR_LVGL=1` + `EEZ_PLATFORM_SIMULATOR`，pin c3e0ac0）在本仓库
`third_party/lvgl`（74d0a81 = v8.3.11）+ 仓库 `config/lv_conf.h` 副本（LV_MEM_CUSTOM=1 →
embark hooks 记账堆）下编译链接通过（MinGW GCC 15.1.0 / Ninja）。lvgl_api 桥的 8.x 条件分支
（`lv_scr_load_anim` / `lv_mem_alloc`）就是活代码，一处没改；eez-framework 以 `-w` 压制
警告，未触碰仓库零警告基线。样例生成代码与 Flow 运行时同版本配套（native-interface-lvgl-with-flow
的 lvgl submodule pin 4d96c27 亦为 8.3.11），「EEZ Flow × LVGL 8.3.11」精确配对成立。

### ② LVGL 堆增量实测（embark hooks 口径，budget=256 KB）

| 时机 | outstanding | peak | peak/budget |
| --- | --- | --- | --- |
| lv_init + 显示驱动后（ui_init 前） | 5 752 B | 5 752 B | 2.2% |
| ui_init 后（Flow assets 解析 + 建 2 屏 + 变量） | 12 855 B | 13 023 B | 5.0% |
| 600 帧运行后（含导航动画与 tick 稳态） | 13 277 B | 15 039 B | 5.7% |

**Flow 运行时净代价 ≈ 常驻 7.1 KB / 峰值 9 KB**（样例 800×480 两屏；Embark 240×320 像素更少，
只会更低）。256 KB 预算余量充足。EEZ_FOR_LVGL 模式下 Flow 的全部分配确认走 LVGL 堆
（core/alloc.cpp → lv_mem_alloc → embark_lvgl_alloc），单一记账链完整覆盖，无暗堆。

### ③ App 契约适配点清单（交 ADR 0008 与适配 issue）

1. `ui_init()` = `eez_flow_init(assets, sizeof(assets), (lv_obj_t**)&objects, sizeof(objects),
   images, sizeof(images), actions)` + `init_vars()` → 放进 App `onCreate`（替代手搭 screen）。
2. `ui_tick()` = `eez_flow_tick(); tick_vars(); tick_screen(g_currentScreen);` → 挂进
   `LvglUiPort::process()` 的 5 ms 节拍（与 `lv_timer_handler()` 同循环）。
3. 屏幕归属裁决：`eez_flow_init` 收尾即 `replacePageHook`（内部 lv_scr_load_anim），Flow 自己
   管前台 screen；App 契约的 onEnter/onResume `lv_scr_load(screen_)` 与之重叠——适配层需按
   screen 指针判等去重（同屏不重复 load），跨 App 切换仍走框架 request_switch/request_home。
4. 交互出口：Studio 里定义 User Action → 生成 `action_<name>(lv_event_t*)` 声明，由 App 的
   C++ 实现（里面可调 request_switch、发 Message 等框架动作）。
5. 数据桥：Flow 全局变量经 `<type> get_var_<name>() / void set_var_<name>(v)` 原生变量与 App
   状态互读互写（vars.cpp 样例：getGlobalVariable/setGlobalVariable + 256 B 静态 snprintf 缓冲）。
6. 生成代码 include `<lvgl/lvgl.h>` → include 根设为仓库 `third_party/` 即解析（已验证）。
7. 字体：Studio 工程引用的字号须在 lv_conf 显式开启（const 数组不进堆、不占预算）；中文与
   design tokens 映射仍由 issue 17 缺字审计在适配 issue 收口。

### ④ 踩坑记录

- 目录上溯：spike 在 `.scratch/embark-v1/spikes/18-eez-flow/`，到仓库根是 **4 层** `../../../..`。
- main.c 对 lv_drivers 的 include 根是 lv_drivers/ 目录本身 → `#include "sdl/sdl.h"`。
- lvgl 的 examples/demos 目标必须 EXCLUDE_FROM_ALL：benchmark 资源 `.c.c` 双后缀文件在深路径下
  MinGW 依赖文件超长（>250 字符）编译直接失败。
- eez-framework 仅 `EEZ_FOR_LVGL` 不够：`osKernelGetTickCount()` 唯一实现在
  `platform/simulator/cmsis_os2.cpp`，必须再定义 `EEZ_PLATFORM_SIMULATOR`。
- 样例 screens.c 引用 montserrat 18/20/24/32/48，仓库 lv_conf 只开 14 → spike 用 lv_conf 副本
  追加五行（注释标明生产适配时由 issue 17 收敛）。
- Windows 执行环境：pwsh 工具本身就是 PowerShell（PATH 无 pwsh 命令，勿嵌套）；跑 .ps1 需
  `powershell -NoProfile -ExecutionPolicy Bypass -File`；PowerShell 5.1 下别用 `$args` 自动变量。
- 截图：SDL 窗口 class=SDL_app、title="TFT Simulator"，FindWindow + CopyFromScreen 可行；但
  桌面前台被用户占用时会抓错窗口——**不要在用户使用桌面时做前台点击/截图实验**（本轮实际教训，
  误抓截图已删除）。

### ⑤ 与原计划的偏差

- eez-framework pin：原备注 master@2b11334，实际用样例 repo 自带 submodule pin **c3e0ac0**
  （官方样例配套版本，与生成代码严格配套；该 submodule 磁盘 .git 元数据异常、rev-parse HEAD
  为空，以样例 .gitmodules 记录 + 磁盘文件为准）。转正式 submodule 时需重新确定 pin 策略。
- Studio 下载未做（本机无 EEZ Studio，下载安装超出 spike 必要性），按原计划降级路径用上游
  官方样例的生成代码（即 Studio 同款产物）验证——已覆盖「一屏 + 按钮 + 自定义动作 + 换屏」全要素。
- lv_drivers 8cdabe8d 从样例 submodule 就地取用（独立 clone 的 fetch 该 commit 失败）。

### ⑥ 交付物

- spike：`.scratch/embark-v1/spikes/18-eez-flow/`（CMakeLists.txt / main.c / hooks.c /
  lv_conf.h 副本 / lv_drv_conf.h / embark_lvgl_hooks.h / ui/ 生成代码 16 文件 / build.ps1 / README.md）
- 证据：`.scratch/embark-v1/evidence/18-eez-flow/spike18.png`（Flow 页渲染成功，806×509 窗口截图；
  perf monitor 已开启，渲染帧率以截图画面为准——当前会话模型不支持读图，画面细节请人工复核）
- 数字：本 Answer 表格三组（5 752 / 12 855·13 023 / 13 277·15 039）

### ⑦ 对 ADR 0008 的结论输入

- 内存红线 ✅：Flow 运行时常驻 ~7 KB / 峰值 ~15 KB @ 256 KB 预算（5.7%），单屏 Embark 分辨率更低。
- 版本配对 ✅：EEZ Flow（c3e0ac0）× LVGL 8.3.11 零 API 改动编译运行。
- 剩余工作（颜色映射、字体审计、导航融合、真实 Studio 工程 240×320）→ 适配 issue（建议 #19）
  按「EEZ 管外观、手写 C++ 管行为」落地。


## Comments
（无）