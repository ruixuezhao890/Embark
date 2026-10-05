# EEZ Studio + EEZ Flow 适配：EEZ 管外观与交互流，C++ 管业务

用户提出手写 LVGL 页面太繁琐（2026-10-05），经选型横评后拍板采用 EEZ Studio + EEZ Flow；
spike 18（issue 18，resolved）用官方样例的生成代码在本仓库 LVGL 8.3.11 上零 API 改动
编译运行，实测 Flow 运行时在 LVGL 堆常驻约 7 KB / 峰值约 15 KB（256 KB 预算的 5.7%）。
因此决定：引入 eez-framework（MIT）运行时 + EEZ Studio 生成代码，框架加一层薄适配，
App 契约核心不变。

1. **第三方依赖与生成代码**：eez-framework 按仓库惯例以 submodule 引入
   （锁 issue 18 实测的 c3e0ac0，转正时重定 pin 策略并记录）；Studio 工程文件
   （.eez-project）与导出的 ui/ 生成代码**随仓库入库**——EEZ Studio 是图形化工具、
   构建不能依赖人工点鼠标，生成代码入库才能保证 CI 与任何协作者可复现构建；
   生成代码按第三方压制警告（零警告基线不被污染）。
2. **适配层 = 薄桥接，契约核心不动**：ui_init()（= eez_flow_init + init_vars）装进
   App 的 onCreate；ui_tick()（= eez_flow_tick + tick_vars + tick_screen）由
   **新增的前台节拍钩子**驱动——App 契约补一个可选虚函数 onForegroundTick(now_ms)
   （默认空实现），框架在 UI 任务每跳调用前台 App 的它；EEZ App 覆写它来跑 ui_tick()。
   选它而不是让 LvglUiPort 直接知道 eez：钩子通用、不绑定第三方，且 eez_flow_tick
   必须在 UI 任务上下文（唯一 LVGL 操作点）执行的要求同样满足。
3. **导航融合**：eez_flow_init 收尾自动 lv_scr_load_anim 首页，与 App 契约
   onEnter/onResume 的 lv_scr_load 重叠——适配层按 screen 指针判等去重（同屏不重复
   load）；Flow 的换屏（eez_flow_set_screen / flowPropagateValue 切页）只允许发生在
   **App 内部**，跨 App 切换仍只走框架的 request_switch / request_home，统一返回键
   （ADR 0006）不变。
4. **交互出口 = Flow action**：Studio 里把控件事件定义为 User Action，导出为
   action_<name>(lv_event_t*) 声明，由 App 的 C++ 实现；action 里可调 request_switch、
   发 Message 等一切框架动作——「EEZ 管外观与交互流、C++ 管业务」的边界就在这条线。
5. **数据桥 = 原生变量**：Flow 全局变量经 get_var_<name>() / set_var_<name>(v) 与
   App 状态互读互写；缓冲约定（vars.cpp 样例为 256 B 静态缓冲 + snprintf）沿用。
6. **颜色红线重划**：手写 C++ 仍只准用 design tokens；Studio 页面一律用 Studio 工程
   集中定义的样式（导出为 styles.c，随工程入库）——**样式域归 Studio**。理由：
   硬编码色值若散落手写代码各处是坏味道，但集中定义在一个 Studio 工程里并生成
   styles.c，本质就是「设计令牌的图形化管理」；强行让构建脚本把 styles.c 反替换成
   design_tokens 引用既脆弱（每次导出重跑转换）又违背「用编辑器」的初衷。两条边界
   ——手写代码用 tokens、EEZ 页面用 styles.c——写进 GLOSSARY 与新手文档。
7. **字体红线收口**：Studio 页面用到的 LVGL 内置字号须在 lv_conf 显式开启（spike 已证
   const 数组不进堆）；中文仍走 ADR 0007 静态子集 + 缺字审计，且**缺字审计扫描范围
   扩展到生成代码**（screens.c 等导出文件里的字符串同受 CI 门禁）；不用 Studio 的
   font manager（字源由框架侧提供）。
8. **内存红线**：EEZ_FOR_LVGL 模式下 Flow 分配全走 LVGL 堆（core/alloc.cpp →
   lv_mem_alloc → embark hooks），256 KB 预算与 embark_lvgl_outstanding/peak 观测点
   不变，无暗堆（spike 已实证）。

**否决过的方案**：升 LVGL 9 迁就编辑器（8.3.11 实测零改动可用，版本升级是独立的
难逆转决策，不因编辑器顺带绑架）；SquareLine Studio（1.6.0 已删 8.x、商用付费）；
GUI Guider（免费纯 C 但无 Flow 逻辑编排、版本绑 NXP）；自建声明式 builder（只治
搭版痛、无可视化拖拽，且用户明确要编辑器）；编辑器只出设计稿（不解决生成）；
LvglUiPort 直接调 eez_flow_tick（把第三方耦合进 UI 端口，通用钩子更干净）。

**代价与影响**：新增 submodule（eez-framework）与构建接入（照 spike CMake 集成，
lvgl examples/demos EXCLUDE_FROM_ALL 等踩坑见 issue 18 Answer）；App 契约增加
onForegroundTick（默认空，既有 App 零改动）；App 开发模式分化为「EEZ App（Studio
工程 + C++ action/变量桥）」与「手写 App」并存，前者为默认推荐；缺字审计扩展扫描
生成代码；每跳多一次 eez_flow_tick 开销（峰值内存已量化，CPU 占比待真机实测）。

工作项与验收草案见 .scratch/embark-v1/issues/19-eez-framework-adapter.md。
