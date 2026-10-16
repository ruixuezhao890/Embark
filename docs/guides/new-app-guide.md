# 新手指南：30 分钟加一个带界面的 App

> 目标读者：第一次在 Embark 上加 App 的开发者（人，或 agent）。
> 读完并走完本指南，你会掌握完整的 App 工作流——**跑起来 → 敲起来（C++ 薄壳）→ 改起来
> （AiDoGUI 设计工程）→ 验收**。之后加任何 App 都是同一个套路。
> 前置条件：仓库能构建（见 [quickstart.md](quickstart.md)）；界面来源与生成链路见
> [app/aidogui_ui/README.md](../../app/aidogui_ui/README.md) 与
> [adr/0011-aidogui-ui-source.md](../adr/0011-aidogui-ui-source.md)。
> 只想先看现成样板：`app/music/` 与 `app/album/` 是两个最小的完整 App（薄壳 + 同名占位屏 + 字符表 + 验收），
> 本指南的每个数字都来自它们。
>
> **EEZ 时代的旧指南**（[eez-ui-manual.md](eez-ui-manual.md)、[eez-studio-guide.md](eez-studio-guide.md)）
> 已加废弃横幅，只作历史阅读。

## 0. 三个角色，一张分工表

| 谁 | 管什么 | 在哪做 |
| --- | --- | --- |
| **AiDoGUI Designer** | 画屏、排控件、配"点按钮切屏"、维护字体字符表 | 设计工程 `app/aidogui_ui/screen.aid`（**随仓库入库**，导出产物同样入库） |
| **Embark App（C++）** | 业务逻辑：状态机、定时任务、消息、数据 | `app/<名字>/` 下两个文件，5 个钩子 |
| **薄桥 `app/common/aidogui_ui_bridge.h`** | 生成代码 ↔ App 契约的唯一适配点（按屏名/控件名查找） | App 只调它，**不 include 生成代码头** |
| **导航 `app/common/aidogui_ui_nav.{h,cpp}`** | 屏变化 → 切前台 App；屏上控件 → App / 逻辑动作的绑定 | App 只在 `onCreate` 里 `ui_backend_attach_nav(fw)`（要绑逻辑动作再加一句 `ui_backend_bind_action`；有控件值要重推再加一句 `ui_backend_bind_repaint`） |

铁的约定只有三条（[ADR 0011](../adr/0011-aidogui-ui-source.md)）：

1. **屏名 == App 名**（`name()` 返回 `"music"` ⇒ 设计器里屏的 id/name 都得是 `music`）；
2. **生成代码不手改**（`app/aidogui_ui/export/` 每次导出整体覆盖，改界面去设计器/工程文件）；
3. **数据接口走控件表按名查找**（`aidogui_ui_bridge_widget("屏名", "控件名")`），App 不直接引
   `ui_<屏id>_<控件名>` 全局——控件改名不会静默断链。

另外一条**免费得到**的能力（不用你写一行）：

- **主屏磁贴**：磁贴由注册表运行时生成（[ADR 0013](../adr/0013-launcher-paged-tiles.md)），
  **加 App 自动多一个磁贴**（每页 4 个，超过自动多一页 + 多一个页码点），不用改设计稿。

**回程不免费，要你自己摆**：新 App 的屏里放一个「返回」按钮，在设计器里给它挂「调用函数」
`embark_ui_open_launcher`（照 clock / weather / music / album / settings 六张现成屏抄），点击即回
主屏。框架**没有**统一返回手势或返回键——早先的非主屏上滑返回已按用户要求撤回
（[ADR 0014](../adr/0014-in-screen-back.md)）。

屏上的按钮要**就地干点事**（不切屏，比如设置的「亮度 +1」）时，别自己给控件加事件回调：在
`onCreate` 里声明一句 `ui_backend_bind_action("屏名", "控件名", &回调)`，回调由导航桥在每次装载后
统一重挂（控件随屏销毁消失，自己挂会点一下就切两次；见 [ADR 0014](../adr/0014-in-screen-back.md) 决策 2）。

还有两条**默认就该照做**的约定（[ADR 0015](../adr/0015-app-retention-and-refresh.md)）：

- **不要申请后台**：`settings()` 返回 `AppSettings{}`（suspend）就行。「只有看得见时才需要刷新」的显示
  （时间、天气）走 `onForegroundTick`（每帧、仅前台）或进屏刷新，**不要**为它声明 `tick` / `own_task`；
  只有「离开后必须继续推进」的 App（放歌、下载）才申请，并写清理由。
- **写了控件值就声明重推**：屏每次装载都是**新建**的（值 = 设计稿初值），在 `onCreate` 里
  `ui_backend_bind_repaint("my", 回调)` 声明一次，桥会在每次屏装载后调它，在里面把控件值重写一遍；
  要留存的位置（页码、翻到的月份）存成成员变量——照 `app/clock/clock_app.cpp` 与
  `app/launcher/launcher_app.cpp` 抄。不声明的后果：控件停在设计稿（时间停在 `11:31` 就是这么来的）。

## 1. 跑起来（10 分钟）

打开终端（Windows 用 PowerShell / pwsh 都行）：

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    ctest --test-dir build --output-on-failure
    ./build/platform/host/embark_host_ui          # Windows 用 .\build\platform\host\embark_host_ui.exe

> **Windows 上把构建目录放短路径**（例如 `-B C:\eb\app`，别用仓库里的 `build/`）：
> 仓库 cwd 很长时，LVGL 的依赖文件路径会超过 250 字符，编译期直接报
> `fatal error: opening dependency file third_party\lvgl\CMakeFiles\lvgl.dir\src\draw\...obj.d: No such file or directory`。
> 这是路径长度问题，不是代码问题。

看到 240×320 的竖屏窗口里：

- 标题「启动器」+ 右上角实时时间；时钟卡、天气卡；
- **磁贴带**：第 1 页「时钟 / 设置 / 天气 / 音乐」，下面 2 个**页码点**（居中）；
- 点磁贴进 App；**点屏里的「返回」按钮回主屏**；左右拖动磁贴带翻页（松手吸附）。

四条验收命令（**带合成点击/手势的必须给 `--delay 40`**，见 §4）：

    ./build/platform/host/embark_host_ui --ui                          # 字体/屏表/切走即释放
    ./build/platform/host/embark_host_ui --ui-nav --delay 40           # 点磁贴 → 进屏 → 返回
    ./build/platform/host/embark_host_ui --ui-launcher-pages --delay 40  # 分页 + 设置屏三段 + 字形守卫 + 留存/重推不变量
    ./build/platform/host/embark_host_ui --ui-weather --delay 40       # 天气屏与主屏卡片同源

每条都会打一行结论（例如 `〔主屏分页验收通过〕（磁贴 5 / 页 2 / 点 2；设置屏 已进 亮度档 0→1；…）`），
退出码 0 = 通过、非 0 = 失败。

## 2. 敲起来（10 分钟，C++ 侧）

照 `app/music/` 抄两个文件（`music_app.h` / `music_app.cpp`），改名即可。最小形状：

```cpp
// app/my/my_app.h
class MyApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "my"; }     // 必须等于屏名
  [[nodiscard]] const char* title() const override { return "我的"; }  // 主屏磁贴上的字
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_EDIT; }  // 预留，磁贴现在是纯文字
  [[nodiscard]] AppSettings settings() const override { return AppSettings{}; }  // 后台策略，默认 suspend

  void onCreate(Framework& fw) override;   // 只记账 + 存 fw_
  void onEnter() override;                 // 装自己的屏
  void onPause() override;                 // 只打日志
  void onResume() override;                // 回前台再装一次屏
  void onExit() override;                  // 只打日志
  void onForegroundTick(std::uint32_t now_ms) override;  // 推数据 + ui_backend_tick()
 private:
  Framework* fw_ = nullptr;
};
```

```cpp
// app/my/my_app.cpp
#include <embark/framework.h>
#include <embark/log.h>
#include "my_app.h"
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
#include "ui_backend.h"
#endif

void MyApp::onCreate(Framework& fw) { fw_ = &fw; }
void MyApp::onEnter() {
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());   // 屏名 == App 名；缺屏只记日志、不黑屏
#endif
}
void MyApp::onResume() { /* 同上：回前台时屏可能已被别的 App 覆盖 */ }
void MyApp::onPause() {}
void MyApp::onExit() {}
void MyApp::onForegroundTick(std::uint32_t) { /* 有数据时在这里 ui_backend_tick(); */ }
```

`ui_backend.h` 是**唯一接缝**（[ADR 0011](../adr/0011-aidogui-ui-source.md)）：宏选后端
（`EMBARK_AIDOGUI_UI` 优先），真机当前没有 UI 后端时四个函数编译成空操作，App 照常编过。

**注册（两处，别漏）**：

1. `app/CMakeLists.txt` 的 `embark_demo_apps` 源列表加 `my/my_app.cpp`；
2. `platform/host/ui_demo.cpp` 的 `EMBARK_APP_TABLE(...)` 加 `embark::demo::MyApp`。

> **注册顺序就是主屏磁贴顺序**（第 0 个是主屏自己，不出磁贴），也是默认前台 App。
> 排在后面 = 在第 2 页。`max_apps` 现在是 12（= 3 页），超了是编译期 `static_assert`。
> 真机清单 `platform/esp32/project/components/embark/CMakeLists.txt` 在第三步（真机界面）之前
> **不用动**；`platform/host/ui_tour.cpp` 与 `user_main.cpp` 是精简系统用例，也不必加。

**一键生成脚本的现状**：`tools/scaffold_app.py` 仍在，但它生成的是 **EEZ 时代的变量骨架**
（AiDoGUI 没有变量层），生成出来的 App 不会挂屏、不该照抄。**现在照 `app/music/` 抄更快**；
脚本与其余工具见 [scaffold-and-tools.md](scaffold-and-tools.md)（已标出废弃范围）。

**还没画屏时会怎样**：`ui_backend_load_for_app` 找不到同名屏 → 保持当前屏 + 一行 INFO 日志
（`AiDoGUI 工程里还没有 App my 的屏（约定：screen == App 名）`），**不会黑屏**。
所以 C++ 可以先敲、先构建、先跑通，再去做界面。

## 3. 改起来（10 分钟，设计侧）

**A. 打开工程**：AiDoGUI Designer → 首页「打开工程」→ 选 `app/aidogui_ui/screen.aid`
（设计器会在这里建 `.ai-workspace/`，已 gitignore）。

**B. 新建屏**：

- 屏 id 与 name **都要等于 App 名**（`my`）；画布 240×320；
- 控件 **id 必须是 `屏id_控件名`**（如 `my_Btn_Back`），**parentId 必须是同屏已存在的控件 id**
  —— 这两条是生成器的硬校验，违反时导出直接失败并报
  `控件"Card_Time"：ID 必须为屏幕ID_控件名称`；
- 最省事的做法：**克隆一个占位屏**（music / album 的 7 个控件：`Desk` / `Btn_Back` + 文字 /
  `L_Title` / `Card_Placeholder` + 卡内两行字），逐个改 id 前缀与文案 —— 颜色、字号、圆角照抄。

**C. 挂导航**：选中按钮 → 添加事件 → 动作选「**调用函数**」→ 函数名 `embark_ui_open_launcher`
（回主屏）或 `embark_ui_open_<目标 App 名>`。可选函数名清单 =
`app/common/aidogui_ui_hooks.h` 的 `EMBARK_AIDOGUI_HOOK_APPS`
（**这张表只列"能被界面动作打开的 App"**；只从主屏磁贴进的 App 不用加）。
`export.ps1` 会数生成代码里的 `embark_ui_open_*`：0 条告警、名字没在表里直接报错。
> 不要用 `CHANGE_SCREEN`：`_ui_screen_change()` 绕过桥的"按需 init/destroy"，会破坏"切走即释放"。

**D. 推数据**（界面要显示动态值时才需要）：AiDoGUI **没有变量层**，App 侧按名字拿控件：

```cpp
if (lv_obj_t* label = aidogui_ui_bridge_widget("my", "L_Value")) {
  lv_label_set_text(label, buf);   // 值变了才写；屏未装载时返回 nullptr
}
```

**E. 补字符表（每次都做，最容易忘）**：工程里 `i18n.customCharsetRows` 是**显式维护**的，
每个 (字体, 字号) 一行，只收你写进去的字符。加一个 App 至少要：

- **14px 行**加新 App 的 `title()` 全部字符（主屏磁贴标题用 14px，漏了就是方框）；
- **12px 行**加屏上小字（提示行 / 占位说明）；屏上其他文字按各自字号行补；
- ⚠ `ui_heiti.ttf` **没有中点 `·`（U+00B7）**：写进字符表会让导出直接失败
  （`Font "ui_heiti.ttf" doesn't have glyph U+00B7`）。要分隔符就用全角逗号。

漏字符**不会报错**，只会画成方框 —— 所以验收侧有字形守卫
（`--ui-launcher-pages` 逐字符 `lv_font_get_glyph_dsc`，见 §4）。

**F. 导出**（两条路，产物相同）：

- GUI：菜单「导出 C 代码」向导 → 输出目录选 `<仓库>/app/aidogui_ui/export`，图片 / 字体格式都选 **BIN**；
- 无头（CI / agent 首选）：

```powershell
# 需要能找到 AiDoGUI 安装目录：AIDOGUI_HOME → .aidogui-home → tools/aidogui/home.txt
pwsh -NoProfile -File tools/aidogui/export.ps1
```

它打印「完成：代码 N 个 · 字体BIN M 个 · 屏：…」与「导航事件：embark_ui_open_* xK」——
**屏列表里没有你的新屏 = 没导成功**。

> ⚠ `export.ps1` 是**先整目录删掉 `export/` 再重建**：生成失败会让工作树一片删除。
> 恢复只要一句 `git checkout -- app/aidogui_ui/export`，报错内容就是失败原因，照着改工程文件再跑。

**G. 构建**：`cmake --build build`。屏表 / 控件表 / 字体表不入库，由 CMake **配置期**解析
`export/`（导出产物变了会自动重跑配置），配置日志会打印屏表。

## 4. 验收（5 分钟）

```sh
ctest --test-dir build --output-on-failure              # 内核 + 分页纯函数单测
./build/platform/host/embark_host_ui --ui                 --frames 40
./build/platform/host/embark_host_ui --ui-nav             --delay 40
./build/platform/host/embark_host_ui --ui-launcher-pages  --delay 40
./build/platform/host/embark_host_ui --ui-weather         --delay 40
```

四条都要退出码 0。**你的新 App 至少要出现在 `--ui-launcher-pages` 的磁贴清单里**
（它会核对磁贴数、顺序、每格文案，并对每个磁贴逐个字符做字形检查）。

| 情况 | 行为 |
| --- | --- |
| 没画同名屏 / 屏名拼错 | 保持当前屏 + INFO 日志（不黑屏） |
| 磁贴标题字形缺失 | 主屏画方框；`--ui-launcher-pages` 的字形守卫变红 |
| 控件名拼错（`bridge_widget` 找不到） | 返回 `nullptr`，App 侧判空即可（不崩） |
| 生成器校验不过（id / parentId / 字体） | 导出失败且 `export/` 被清空 → `git checkout -- app/aidogui_ui/export` |
| `--ui-nav` 时好时坏 | 忘了 `--delay 40`（见下） |

> **带合成点击/手势的验收必须给 `--delay 40`**：LVGL 约每 30 ms 读一次输入；帧跑得比它快时，
> 一帧里推的"按下 → 移动 → 抬起"会被采样进同一次读取 —— 同一份脚本会一趟过一趟不过
> （点击晚十几帧生效、拖动看着没吸附）。

## 5. 踩坑清单（新用户第一周必看）

1. **屏名 ≠ App 名** → 挂不上屏（症状：一直停在上一屏 + 一行 INFO 日志）。
2. **控件 id 不是 `屏id_名`、或 parentId 指到别的屏** → 生成器报错、`export/` 被清空（照报错改工程文件再跑）。
3. **字符表漏字** → 屏上是方框；**中点 `·` 一定不要写**（`ui_heiti.ttf` 里没有，导出直接失败）。
4. **手改 `export/`** → 下次导出被覆盖；要改界面就改设计器 / 工程文件，导出后重新构建。
5. **忘了把新 App 加进注册表** → 磁贴不出现、也切不过去（注册表是唯一入口）。
6. **注册顺序**决定磁贴顺序与默认前台：想让新 App 排在第 1 页，就把注册位置放在前 4 个里。
7. **Windows 构建路径过长** → `obj.d` 打不开；把 `-B` 指到短路径（如 `C:\eb\app`）。
8. **`onForegroundTick` 里做重活** → 它就跑在唯一 UI 任务里，会拖慢整机；重活 / 阻塞走
   `own_task` 后台策略（[messages-and-background.md](../concepts/messages-and-background.md)）。
9. **`onCreate` 里 publish** → 总线这时还没装配，消息被计为 unknown（放到 `onEnter` 之后）。
10. **App 名重名** → `EMBARK_APP_TABLE` 展开时编译期报错。

钩子契约（几次 / 哪个线程 / 该放什么）见 [reference/hooks-and-api.md](../reference/hooks-and-api.md)；
容量上限见 [reference/limits.md](../reference/limits.md)（`max_apps = 12`）。

## 6. 从这里往哪走

- 界面工程目录、字体 BIN、验收命令细节：**[app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)**
- 界面来源 / 分页与回程的决策：**[adr/0011](../adr/0011-aidogui-ui-source.md)**、**[adr/0013](../adr/0013-launcher-paged-tiles.md)**、**[adr/0014](../adr/0014-in-screen-back.md)**（撤回上滑返回，回程 = 屏内返回按钮）
- 本功能的规格与验收剧本：**[.scratch/launcher-pages/spec.md](../../.scratch/launcher-pages/spec.md)**、**[acceptance.md](../../.scratch/launcher-pages/acceptance.md)**
- 回程改动与两个界面 bug（时钟缓存、天气两字被裁）+ 设置屏：**[.scratch/launcher-fixes/spec.md](../../.scratch/launcher-fixes/spec.md)**
- 后台策略 / 消息总线 / own_task 生命周期：**[messages-and-background.md](../concepts/messages-and-background.md)**
- 脚本与工具（验收开关与退出码部分仍然有效）：**[scaffold-and-tools.md](scaffold-and-tools.md)**
