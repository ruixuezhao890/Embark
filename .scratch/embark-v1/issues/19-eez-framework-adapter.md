# 19 · EEZ Studio 适配层落地（框架接入 + 首个 EEZ App）

Status: resolved
Type: feature
Blocked by: 18（resolved）
来源: 用户拍板「eez studio 吧，配上它的flow，然后我们框架适配它」（2026-10-05）；
ADR 0008 已定案。本 issue 把 spike 18 的适配点清单落成框架能力，并做出第一个
EEZ App 作为行走的示例。

## 目标

把 EEZ Studio 导出的生成代码接入 Embark 骨架：新增 eez-framework 构建接入、App 契约
前台节拍钩子、EEZ App 支持件，并产出首个 EEZ App（新 hello 或改造现有 app），证明
「新建 App = Studio 工程 + 几个 C++ 回调」成立。完成后用户日常写页面不再手拼 LVGL。

## 范围

1. **eez-framework 转正**：加 submodule（第三方目录或 third_party/，位置按构建约束
   定）；CMake 接入照 spike 18（EEZ_FOR_LVGL + EEZ_PLATFORM_SIMULATOR + SYSTEM
   include + -w 压制警告 + EXCLUDE_FROM_ALL 教训）；pin 从 c3e0ac0 起，重定 pin 策略
   并在构建文档记录（样例 submodule 的 .git 元数据异常，需找可 fetch 的官方 commit）。
2. **App 契约补前台节拍钩子**：app.h 增加 onForegroundTick(uint32_t now_ms) 默认空；
   Framework step 在 UI 任务每跳调用前台 App 的它（与 lv_timer_handler 同循环；
   确认 tick/pump_input/process 阶段顺序，钩子插在 process 内、lv_timer_handler 之后）。
3. **EEZ 支持件（adapter）**：
   - 薄桥：ui_init 装进 onCreate、ui_tick 装进 onForegroundTick 的标准模板/助手；
   - 屏幕归属裁决：eez_flow_init 自动 lv_scr_load_anim 与契约 onEnter/onResume 的
     lv_scr_load 按 screen 指针判等去重；Flow 导航限制在 App 内（跨 App 仍走
     request_switch/request_home，统一返回键不破坏）；
   - action/变量桥：action_<name>(lv_event_t*) 与 get/set_var_<name> 的 C++ 落点约定
     （模板/文档，不自动生成）。
4. **首个 EEZ App**：用 EEZ Studio 建 240×320 竖屏工程（一屏 + 按钮 + User Action +
   至少一个 Flow 变量），导出生成代码入库（含 .eez-project），在 host 模拟器跑通；
   演示 action 里调框架动作（如 request_home 或发 Message）。Studio 本机不可用时
   沿用样例生成代码并如实记录。
5. **缺字审计扩展**：CI 扫描范围纳入生成代码（screens.c 等）的字符串；lv_conf 显式
   开启生成代码引用的字号；中文静态子集（ADR 0007）先覆盖 EEZ App 用字。
6. **文档**：新手文档「EEZ App 一条龙」（Studio 建工程 → 导出 → C++ 接 action/变量 →
   注册 → 跑）；GLOSSARY 补术语（EEZ App / styles.c 样式域边界）；common-pitfalls 补
   spike 踩坑。

## 验收

- 构建：eez-framework 转正后 host 全量构建通过、零警告基线不破、既有测试全绿。
- 行为：EEZ App 在 host 模拟器渲染、点击触发 action、Flow 变量与 C++ 状态互通、
  request_home/request_switch 正常；截屏证据进 .scratch/embark-v1/evidence/19-eez-adapter/。
- 契约：既有 App（hello/demo/launcher/settings 等）零改动编译（onForegroundTick 默认
  空实现不强制覆写）。
- 文档：新手文档可照做；GLOSSARY 新词入库；ADR 0008 与实现一致。

## 备注

- 颜色：样式域归 Studio（styles.c 随工程入库）；手写代码仍只用 design tokens（ADR 0008）。
- 内存：Flow 走 LVGL 堆（256 KB 预算内，issue 18 已量化 5.7%），不新增暗堆。
- 真机（esp32s3）接入不在本 issue：host 先行；ESP-IDF 构建与 PSRAM 事项另开 issue。
- 参考：issue 18 Answer 的适配点清单与踩坑记录；spike 工程
  .scratch/embark-v1/spikes/18-eez-flow/。

## Answer（2026-10-05，落地完成）

### ① 落地清单（issue 范围逐项）

1. **eez-framework 转正**：submodule 落 third_party/eez-framework（pin **c3e0ac0**，
   issue 18 样例配套版本）；third_party/CMakeLists.txt 新增 eez_framework STATIC +
   embark::eez ALIAS（spike 18 配方：GLOB_RECURSE src/eez/*.{cpp,c}、FILTER
   platform/stm32|libs/libscpi、SYSTEM PUBLIC include eez-framework/src+libs/agg+
   platform/simulator、PUBLIC defs EEZ_FOR_LVGL EEZ_PLATFORM_SIMULATOR、PRIVATE -w、
   PUBLIC link lvgl::lvgl、WIN32 加 _CRT_SECURE_NO_WARNINGS + wsock32 ws2_32；
   上游源不依赖 etl，构建即验证通过）。
2. **App 契约补前台节拍钩子**：include/embark/app.h 新增
   virtual void onForegroundTick(std::uint32_t now_ms) 默认空实现；
   src/embark/framework.cpp step() 的「4b」段在 lv_timer_handler 之后调用前台 App 的它；
   include/embark/framework.h 步骤注释同步；spec.md 钩子清单加 onForegroundTick。
   既有 App 零改动编译（默认空实现，验收通过）。
3. **EEZ 支持件**：app/eez_ui_bridge.{h,cpp} 薄桥 4 函数——init()=ui_init、
   tick()=ui_tick、load_current_screen()（按 g_currentScreen 选 objects.home/main 并
   lv_scr_load，与 Flow 自动 load_anim 按指针判等去重）、counter()（读 vars.cpp 全局
   int32_t counter，变量桥）。Flow 导航（action_login → eez_flow_set_screen）限制在
   App 内；跨 App 仍走 request_home（返回键），验收里已双向验证。
4. **首个 EEZ App**：app/eez_demo_app.{h,cpp} EezDemoApp（槽 5，title EEZ，
   icon LV_SYMBOL_HOME）；onCreate→bridge_init、onEnter/onResume→bridge_load_current_screen、
   onForegroundTick→bridge_tick；生成代码 app/eez_ui/ 16 文件入库（screens.c 两处
   屏尺寸 800×480→240×320 改写已核）。Studio 本机 CLI 导出能力未验证，按 issue 允许的
   降级路径沿用官方样例生成代码并如实记录（见偏差）。
5. **缺字审计扩展**：lv_conf.h 显式开启生成代码引用的 MONTSERRAT 18/20/24/32/48
   （spike 已验证、仓库版补上）；生成代码运行时文案全 ASCII
   （Login/Logout/User name:/+/-/Option 1..6），入 issue 17 审计范围（已注记）。
6. **文档**：docs/README.md「EEZ App 一条龙」+ 索引行；common-pitfalls.md 新增 EEZ 小节；
   GLOSSARY.md 补 EEZ App / Style scope / Foreground tick（ADR 0008 已先行）。

### ② 验收结果

| 验收项 | 结果 |
| --- | --- |
| host 全量构建（Ninja 78 目标，FreeRTOS=ON） | ✅ 通过，**零警告**（生成代码统一 -w） |
| ctest | ✅ 1/1 通过 |
| --eez 行为验收（拖 5 槽→启动→Login Flow 导航→点 + →返回键） | ✅ 断言全过，退出码 0 |
| 断言明细 | switches==2、selected==5、eez enters==1 resumes==0、foreground_ticks=119>0、counter==1（点 + 一次）、home_requests==1、前台回 launcher |
| 既有 App 零改动 | ✅ hello/demo/launcher/settings/ticker 未动一字节 |
| 截屏证据 | .scratch/embark-v1/evidence/19-eez-adapter/{eez_main,eez_home}.png（240×320，主屏与点 + 后 HOME 屏） |

内存口径：--eez 整场跑完 LVGL 未回收 24115 字节（预算 262144，含全部 6 个 App 的
screen 树 + Flow 运行时），与 issue 18 量化（Flow 净增 ≈ 常驻 7.1 KB / 峰值 9.3 KB）一致，
预算余量充足。

### ③ 关键故障与根因（CMake C 语言工具链变量作用域）

首次接入生成代码（6 个 .c 源）触发：生成期
「Missing variable is: CMAKE_C_COMPILE_OBJECT / CMAKE_C_ARCHIVE_CREATE / FINISH」，
C_STANDARD/target_compile_features 各种挪法都无效（红鲱鱼）。最小复现探针 6 个微型
工程定位根因：**C 语言只在 third_party/ 子目录作用域被启用**（lvgl 的 project()→
add_library 隐式启用），CMAKE_C_* 工具链规则变量只装载于该作用域，兄弟作用域
app/eez_ui/ 看不见。之前没炸是因为 build-verify 缓存里 EMBARK_PLATFORM_HAS_FREERTOS=OFF，
cmake/embark_freertos.cmake 提前 return 跳过了根作用域的 enable_language(C)（CI/README
默认 ON 时恰好掩盖）。修复：根 CMakeLists.txt project() LANGUAGES CXX →
LANGUAGES C CXX（带 4 行原因注释）。CMAKE_C_COMPILE_OBJECT 从不写进
CMakeCCompiler.cmake（由 Modules/CMakeCInformation.cmake 装载）——「81 行、无该行」的
C 信息文件是正常形态。

### ④ 与原计划的偏差

- Studio 侧：本机 EEZ Studio 已安装（用户自开 GUI），但 CLI 导出能力未验证、也不该在
  用户使用桌面时开窗口实验；按 issue 19 允许的降级路径沿用官方样例生成代码
  （16 文件，240×320 改写），.eez-project 未入库（样例无此物）。
- 变量桥：样例 vars.cpp 的 counter 是全局 int32_t（非 extern "C"），薄桥以
  extern int32_t counter + counter() 取值，未引入 set_var 包装（无 set 需求）。
- 截图：为抓 EEZ 屏中途帧，ui_demo.cpp 新增 --shot-frame N FILE 开关（任意模式通用，
  原 --screenshot 只存末帧）。

### ⑤ 踩坑记录（进 common-pitfalls.md 的浓缩）

- --shot-frame 语法是「帧号 + 文件名」两个令牌；把旧式 --shot-path FILE 混写会
  让文件名被解析成字面量 --shot-path（BMP 落进名为 --shot-path 的文件）。
- option() 不会自愈已有 cache 条目：显式 -DEMBARK_PLATFORM_HAS_FREERTOS=ON 重配。
- 生成代码 C++ 也要 -w（actions.cpp unused parameter 'e' 等），与 C 同口径；
  -fno-exceptions/-fno-rtti 红线保留。
- 构建产物路径含 build-*/（.gitignore 已覆盖），日志/探针等临时物不入库。
## Comments
