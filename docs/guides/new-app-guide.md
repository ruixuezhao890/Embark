# 写你的第一个 App（给只会 Arduino 的人）

> **这节解决什么问题**：你在 launcher 里能点开 clock 了，现在想自己做一个 App。
> 这篇从「一条命令建 App」讲到「后台策略、状态机、消息、日志、容器怎么用」，
> 全程宿主模拟器，不碰真机。所有代码都是在本仓实测编译 + 运行过的。

---

## 0. 这篇给谁 · 必读 · 5 分钟

你会玩 Arduino：写过 `setup()` / `loop()`、`digitalWrite()`、`Serial.println()`。
你没写过 C++ 的类，没用过 STL，看到 `template<typename...>` 就想关掉。这篇是给你写的。

**读完你会**：一条命令建出 App；知道 13 个可覆写点各自该写什么、绝对不能写什么；
用状态机和消息把逻辑理顺；判断自己的 App 要不要后台 —— 以及为什么**通常答案是"不要"**。
**不会讲的**：烧真机、改 HAL、动 `host/`、`device/`（那是另一条线，见 `device/README.md`）。
全程宿主模拟器：一个窗口，改代码 → 编译 → 看窗口。

**先决条件**：你已按 [getting-started.md](getting-started.md) 跑起 launcher、能点进 clock。
这篇从「launcher 里多出一块写着"秒表"的磁贴」开始。

| 词 | 是什么 | 在哪 |
| --- | --- | --- |
| **App** | 一个类（`class`），一个 `.h` + 一个 `.cpp` | `app/<名字>/` |
| **屏** | 设计器里画的一张界面 | `app/aidogui_ui/screen.aid`；**屏名必须等于 App 名** |
| **钩子** | 框架在特定时刻调你的成员函数 | 你的 `.cpp` 里 |
| **可覆写点** | 框架留给你的插槽 ＝ 基类里那些 `virtual` 函数。一共 **13 个**（6 必写 + 7 有默认），**集合是冻结的**（§3）。<br>别的文档里说的「**8 个钩子**」= 这里的 8 个行为钩子（5 个生命周期 + 前台节拍 + 后台节拍 + 消息）；它和「4 个元数据 + `settings()`」加起来正好是这 13 个 —— 两种数法不冲突 | `core/include/embark/app.h` |

> **一句话世界观**：Arduino 把所有事塞进一个 `loop()`；Embark 把 `loop()` 拆成
> 「什么时候该干这件事」的几个钩子 —— 前台每帧一次的是 `onForegroundTick`，
> 第一次进前台的是 `onEnter`，收到消息的是 `onMessage`。
> **你不再自己写 `loop()`，而是往对应的钩子里放对应的活。** 这是全篇最重要的一句话。

### 0.1 怎么读这篇（别从头读到尾）

每节标题后面标了「必读」或「需要时读」。按你现在的处境挑：

| 你现在的处境 | 读哪几节 | 大概耗时 |
| --- | --- | --- |
| 一行代码还没写 | §1 → §2 | 60 分钟（先把空屏点亮，有成就感） |
| 空屏亮了，要写业务 | §3 → §4 | 90 分钟（读透这两节，日常 App 就够写了） |
| 要"切走以后还在跑" | §5 | 20 分钟 |
| 要"收到消息就换行为" | §6 | 30 分钟 |
| 打日志 / 拼字符串 / 用容器 | §7 §8 §9 | 40 分钟 |
| 编译过了但界面不对劲 | §11（§10 瞄一眼） | 随手查 |

---

## 1. C++ 生存包 · 必读（这段比学 C++ 便宜一百倍）

本文所有示例只用下面这些，你不需要学更多。

### 1.1 一个 App 就是一个 `class`

`class` = Arduino 里「一堆函数 + 一堆全局变量」打个包起个名字。唯一区别：
变量从"全局"变成**只属于这个 App**（成员变量），于是名字不会和别人撞车。

```cpp
class StopwatchApp final : public App {   // 「我是一个 App」
 public:                                   // 下面这些框架要调
  void onEnter() override;                 // 只有声明，实现写在 .cpp 里
 private:                                  // 下面这些只有我自己用
  std::uint32_t started_ms_ = 0;           // 成员变量，惯例以 _ 结尾
};
```

### 1.2 `.h` 放声明，`.cpp` 放实现；`override` 是护栏

`.h` = 名片（有什么函数、什么变量），`.cpp` = 正文。**规矩：你只改 `.cpp`**，
`.h` 里那些你没见过的行是框架要求的形状，照抄别动。
`override` 意思是「我在覆盖基类里那个同名函数」：名字打错时编译器会**骂你**，
而不是默默变成一个新函数、然后框架永远调不到它。看到 `override` 就当它是拼写检查，别删。

### 1.3 只用这些类型

| 写法 | 意思 | 什么时候用 |
| --- | --- | --- |
| `int` | 整数 | 小计数、索引 |
| `std::uint32_t` | 无符号 32 位整数（0 ~ 42 亿） | **毫秒时间戳、计数**（`now_ms` 就是它） |
| `bool` | `true` / `false` | 开关 |
| `const char*` | 字符串字面量，如 `"stopwatch"` | App 名、标题 |
| `lv_obj_t*` | 指向屏幕上一个控件的指针 | 拿到控件后改文字 / 颜色 |

**不要出现**：`std::string`、`std::vector`、模板、lambda（`[](){}`）、`new`/`delete`、`throw`。
前几个编不过或引入你没准备好的坑；后两个是本仓硬规矩（§10 红线表）。

### 1.4 指针只有两句话

```cpp
fw_ = &fw;    // 记住 fw 的地址（框架把"自己"递给你，你存下来以后用）
fw_->hal()    // 顺着地址访问 → 拿到硬件能力（时钟、存储、日志）
```

`nullptr` = 「空」。控件可能不存在（设计器里名字写错了），所以拿到 `lv_obj_t*` **一定先判空** ——
这是 §4 里唯一需要你操心的防御性代码。

### 1.5 Arduino → Embark 对照表

| Arduino | Embark | 差别 |
| --- | --- | --- |
| `setup()` | `onCreate()` 然后 `onEnter()` | 拆成两半：装配 / 第一次上场（§3） |
| `loop()` | `onForegroundTick(now_ms)` | 只在你这个 App 在前台时才被调 |
| 全局变量 | 成员变量 `xxx_` | 别的 App 碰不到 |
| `millis()` | `now_ms` 参数 | 框架帮你取好了，别自己去问 |
| `delay(1000)` | **不存在** → 记时间戳、下次比差值 | 这里是 ui 循环，阻塞 = 整机卡（§3.8） |
| `Serial.println()` | `ELOG_INFO("...", 参数)` | 看控制台输出（§8） |
| `Serial.printf("%d")` | `ELOG_INFO("{}", 值)` | 占位符是 `{}` 不是 `%d`（§7） |
| 一个 `.ino` | 一个 `app/<名>/` 目录 | 还要在 3 个地方登记（§2，脚本代劳） |

### 1.6 四种必遇的编译报错

| 报错里出现 | 真实原因 | 干什么 |
| --- | --- | --- |
| `no member named 'xxx' in 'App'` | 钩子名拼错了 | 回来抄 §3 表格里的名字，逐字对 |
| `undefined reference to StopwatchApp::onEnter()` | `.h` 声明了，`.cpp` 没写实现 | 到 `.cpp` 补上函数体 |
| `'xxx' marked 'override', but does not override` | 基类里**没有**这个函数（名字对了但签名错了，比如漏了参数） | 核对参数与 `const` |
| `use of deleted function` / `exception handling disabled` | 你用了 `new` 或 `throw` | 删掉，换静态变量 / 直接打日志 |

---

## 2. 15 分钟做出一个能跑的 App · 必读

### 2.1 一条命令生成骨架

在仓库根目录：

```bash
node tools/new_app.mjs stopwatch --title 秒表 --apply
```

（`--title` 最多 5 个汉字，磁贴放不下更长。不加 `--apply` 是只看计划、不动文件 —— 建议第一次先干跑看看。）

它做的事，正好解释了「App 是怎么被系统认出来的」：

| 它改的 | 为什么 |
| --- | --- |
| 新建 `app/stopwatch/stopwatch_app.h` 和 `.cpp` | 你的代码，**从这一刻起归你**，脚本不会再回来改 |
| `core/cmake/embark_sources.cmake` 加 2 行 | 让编译器知道有这个文件 |
| `host/ui_demo.cpp` 加 2 行（include + App 表里加一项） | **App 表 = 注册表**：写进这张表的顺序就是默认前台顺序 |
| `app/common/aidogui_ui_hooks.h` 加 1 行 | 让界面上的按钮能「打开这个 App」 |
| `app/aidogui_ui/screen.aid` 里加一张同名占位屏 | **屏名必须等于 App 名**（`stopwatch`），否则点进去是一屏黑 |

### 2.2 把屏导出成 C 代码

`screen.aid` 是设计器的工程文件，编译不了。必须导出：

```bash
pwsh -NoProfile -File tools/aidogui/export.ps1
```

生成的代码落在 `app/aidogui_ui/export/`，**那是生成物，永远别手改**（下次导出会整目录重建）。

### 2.3 编译、运行

```bash
cmake -G Ninja -B build                              # 第一次要配置（约 30 秒）
cmake --build build --target embark_host_ui          # 之后只跑这句
./build/host/embark_host_ui.exe --delay 40
```

窗口出来是 launcher。**磁贴排在最末，翻到那一页多了一块写着「秒表」的磁贴，点进去** ——
你会看到一张标题为「秒表」的页面、一个返回按钮、一句「用 HTML 草稿画这屏」。

**这就是你要的起点：一个亮着的空屏。** 此刻你的 `stopwatch_app.cpp` 里一行业务代码都没有，
但 6 个钩子已经在被调用了。

### 2.4 生成的代码长什么样

`app/stopwatch/stopwatch_app.h`（生成物，头注释已省略）：

```cpp
namespace embark::demo {

class StopwatchApp final : public App {
 public:
  StopwatchApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "stopwatch"; }
  [[nodiscard]] const char* title() const override { return "秒表"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_FILE; }

  [[nodiscard]] AppSettings settings() const override { return AppSettings{}; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

 private:
  Framework* fw_ = nullptr;
  std::uint32_t enters_ = 0;     // ← 模板带来的验收计数器，写业务时删掉
  std::uint32_t resumes_ = 0;    // ← 同上
};

}  // namespace embark::demo
```

`stopwatch_app.cpp` 里，六个钩子都是**空壳**：`onCreate` 存下 `fw_` 然后打一行日志，
`onEnter`/`onResume` 调 `ui_backend_load_for_app(name())`（挂自己的屏），
`onForegroundTick` 调 `ui_backend_tick()`（泵一帧）。**这三行是"界面能显示"的最低要求，别删。**

### 2.5 小结：App 的最小成立条件

1. 一个类，`name()` 返回的字符串**同时是屏名**；
2. 写进某一张 `EMBARK_APP_TABLE`（脚本代劳）；
3. `onEnter` 里挂屏、`onForegroundTick` 里泵帧（模板已给）；
4. 屏在设计器里存在并已导出。

四个条件缺一个的表现分别是：磁贴不出现 / 磁贴不出现 / 点进去黑屏 / 点进去黑屏 + 控制台两条 WARN。

---

## 3. 13 个可覆写点：每一个该放什么 · 必读 · 这节是全篇的骨架

App 基类在 `core/include/embark/app.h`。它有 **13 个你能覆写的东西：6 个必须写，7 个有默认实现**。
"可覆写点"就是框架给你留的插槽，它的集合是冻结的（`app.h:14-15` 明写「除此之外不再增加」）——
所以你只要把这 13 个认全，就不会遇到"还有什么我没实现"的意外。

### 3.1 总表

| # | 名字 | 必须写? | 何时被调 | 一句话职责 | 详见 |
| --- | --- | --- | --- | --- | --- |
| 1 | `name()` | ✅ 纯虚 | 任何时候（框架随时问） | 身份，且**必须等于屏名** | §3.3⑥ |
| 2 | `onCreate(Framework&)` | ✅ 纯虚 | 装配期，**恰好 1 次** | 存 `fw_`、取 HAL 能力 | §3.3① |
| 3 | `onEnter()` | ✅ 纯虚 | **≤1 次**：第一次成为前台 | 挂屏、启动状态机 | §3.3② |
| 4 | `onPause()` | ✅ 纯虚 | **每次**离开前台 | 存档、停自己的节拍 | §3.3③ |
| 5 | `onResume()` | ✅ 纯虚 | **每次**回到前台 | **重新挂屏**、恢复显示 | §3.3④ |
| 6 | `onExit()` | ✅ 纯虚 | **恰好 1 次**，且只在关机 | 收尾、落盘 | §3.3⑤ |
| 7 | `title()` | ➖ 有默认 | 画磁贴时 | 给人看的名字（默认 = `name()`） | §3.4 |
| 8 | `icon()` | ➖ 有默认 | 画磁贴时 | `LV_SYMBOL_*`（默认 `nullptr`） | §3.5 |
| 9 | `accent()` | ➖ 有默认 | 需要强调色时 | 主题色（不许写死颜色值） | §3.6 |
| 10 | `settings()` | ➖ 有默认 | boot 时读一次 | 后台策略（§5 专讲） | §3.7 |
| 11 | `onForegroundTick(now_ms)` | ➖ 有默认 | 在前台时**每帧一次** | 把数据推给控件 | §3.8 |
| 12 | `onMessage(const Message&)` | ➖ 有默认 | 每收到一条广播 | 按消息 id 分发（§6） | §3.9 |
| 13 | `onBackgroundTick(now_ms)` | ➖ 有默认 | 后台武装后按周期 | 离开前台也要推进的活 | §3.10 |

前 6 个是**纯虚**：不写编不过。后 7 个有默认实现，但**真实 App 会把 7/8/9 都覆写掉**
（默认的 `title()` 是英文名、`icon()` 是空、`accent()` 是主题色），所以实际动手时你在 `.h` 里是写全 13 个的。

### 3.2 一次完整的生命周期（背这个，比背 13 个函数有用）

```
上电 boot
  ├─ App1.onCreate()  App2.onCreate()  App3.onCreate() …   ← 按注册顺序，全部 App 都要过
  ├─ App1.onEnter()                                        ← 只有默认前台（注册表第 0 个）
  └─ 消息总线装配（所以 onCreate 里发消息没人收，见 3.3 ①）

运行中（ui 循环，5 ms 一跳）
  ├─ 当前前台 App.onForegroundTick(now_ms)  ← 每帧
  └─ 有任何 App.onMessage(msg)              ← 每条广播，所有 App 都收

点磁贴切到 App2
  ├─ App1.onPause()                                        ← 旧的先退场
  ├─ App2.onEnter()  （第一次）  或  App2.onResume()  （回来过） ← 二选一，不会都调
  └─ 之后每帧都是 App2.onForegroundTick()

关机
  └─ 当前前台.onPause()，然后**每个** App 按注册顺序 .onExit() 各一次
```

三条最容易踩错的推论，先记住：

1. **`onEnter` 一辈子只调一次**。第二次进同一个 App 是 `onResume`。
   所以"每次进来都要做的事"（比如重新挂屏、刷新显示）**必须写在 `onResume`**，
   或者两处都调同一个私有函数。
2. **`onPause` 之后你的屏就被卸掉了**（显示器交给别的 App）。别在 `onPause` 里保存 `lv_obj_t*`，
   那个指针下次回来已经失效 —— 回到前台要**重新按名字取控件**。
3. **`onExit` 不是"关掉这个 App"**。v1 没有退役路径，App 常驻到关机；
   `onExit` 只在关机时被调一次。别把清理寄托在它身上。

### 3.3 六个必写钩子：该放什么 / 别放什么

#### ① `onCreate(Framework& fw)` —— 装配，不是"开始工作"

- **该放**：`fw_ = &fw;`（全仓一律这么存）；从 `fw.hal()` 取你要的能力（时钟、存储、日志）；
  建**静态**资源；登记界面回调（`ui_backend_bind_repaint` / `ui_backend_bind_click`）。
- **别放**：`publish()` —— 总线这时**还没装配**，发出去没人收，只会打一条 WARN + 计一次 unknown
  （`core/src/embark/framework.cpp:48-54`，注释原文「装配消息请放到 onEnter 之后」）。
  别碰控件（屏还没加载，取控件返回 `nullptr`）。别做耗时活（这是 boot 期，卡住就是开机慢）。
- **判据**："只做一次、跟你在不在前台无关" → 放这里。

#### ② `onEnter()` —— 第一次上场

- **该放**：挂自己的屏 `ui_backend_load_for_app(name());`；启动状态机 `fsm_.start();`；
  初始化"从头开始"的状态（秒表归零、列表滚到顶）。
- **别放**："每次回来都要做的事" —— `onEnter` **只调一次**（见 3.2 推论 1）。
  想省事就写一个私有 `load_screen()`，`onEnter` 和 `onResume` 都调它 —— 这是全仓通行写法。
- **判据**：只在"这辈子第一次进来"时该做的事 → 放这里。

#### ③ `onPause()` —— 离开前台

- **该放**：把当前状态写进成员变量（`started_ms_`、`elapsed_` 这种）；打一行日志方便排查。
- **别放**：碰界面（你已经不是前台了，屏马上被卸，框架不替你收尾）。
  **别在这里保存 `lv_obj_t*`**，它下次回来已经失效。
- **判据**：离开时要"记住"什么 → 放这里。

#### ④ `onResume()` —— 回到前台

- **该放**：**重新挂屏**（`ui_backend_load_for_app(name())` —— 必须是这句，
  你的屏已经在 `onPause` 时被卸了）；把成员变量里的值重新推给控件。
- **别放**："屏还在，直接改控件" —— 你拿到的是上一轮的野指针。屏每次装载都是**新对象**，
  连点击回调都要重新挂（所以仓库把"重挂 + 重推值"统一放进 `ui_backend_bind_repaint` 回调，
  §4 会写。来龙去脉在 `docs/adr/0015-app-retention-and-refresh.md`，不看也行）。
- **判据**：回到前台要"恢复"什么 → 放这里。

#### ⑤ `onExit()` —— 关机收尾

- **该放**：落盘、打最后的日志。
- **别放**：任何"日常清理"。**它只在关机路径触发一次**，平时永不执行。
  也别以为它会替你停掉后台任务 —— own task 不因为 App 退出而终止（§5）。
- **判据**：只有"整机关掉之前"才该做的事 → 放这里。

#### ⑥ `name()` —— 身份，同时也是屏名

```cpp
[[nodiscard]] const char* name() const override { return "stopwatch"; }
```

返回一个**固定的字符串字面量**，别拼接、别返回临时 buffer（框架只借这个指针，不复制）。
它必须和 AiDoGUI 里的屏名逐字符相同 —— 这不是编译期检查，**写错了不报错，只是点进去黑屏**，
控制台会打一条 WARN「AiDoGUI 工程里没有屏 stopwatch」。重名也没有编译期检查
（`find()` 是运行时线性查找，取第一个命中），所以别起和别人一样的名字。

### 3.4 `title()` —— 磁贴上给人看的名字

```cpp
[[nodiscard]] const char* title() const override { return "秒表"; }
```

- **默认行为**：返回 `name()`，也就是磁贴上显示 `stopwatch` 这个英文单词。
- **该放**：**不超过 5 个汉字**的中文名（240 px 宽的磁贴放不下更长，超了会被截断成"秒…"）。
- **别放**：动态内容（比如"秒表 (运行中)"）—— 它是**常量**，磁贴只在启动/重排时读一次。
- **判据**：磁贴上想显示中文 → 覆写它。这是 7 个默认钩子里你**几乎一定**要覆写的一个。

### 3.5 `icon()` —— 磁贴上的小图标

```cpp
[[nodiscard]] const char* icon() const override { return LV_SYMBOL_PLAY; }
```

- **默认行为**：返回 `nullptr`，磁贴退回"用标题首字符"当图标。
- **该放**：一个 `LV_SYMBOL_*` 常量。可用符号见 `third_party/lvgl/src/font/lv_symbol_def.h`。
- **别放**：自己画的图片、多条字符、emoji。**字形必须在你加载的字体里有**，
  否则显示成方框 —— 不确定就去翻一眼 `app/aidogui_ui/assets/font/ui_heiti.ttf` 加载的那几档字号效果。
- **判据**：想要图标时才覆写；不写也不难看（首字符兜底是设计好的）。

### 3.6 `accent()` —— 这个 App 的专属色调

```cpp
[[nodiscard]] lv_color_t accent() const override { return design_tokens::accent; }
```

- **默认行为**：主题强调色（`design_tokens::accent`），和别的 App 一致。
- **该放**：**从 design tokens 里挑**一个（`design_tokens` 在 `app/common/` 里，是全仓唯一允许出现颜色值的地方）。
- **别放**：`lv_color_hex(0xFF0000)` 这种魔法数。写死颜色 = 换主题时你这块地方不会跟着变，
  而且夜里看着刺眼。
- **判据**：几乎不用覆写。只有"这个 App 在该亮眼的地方必须亮眼"时才覆写。

### 3.7 `settings()` —— 后台策略，默认就是正确答案

```cpp
[[nodiscard]] AppSettings settings() const override { return AppSettings{}; }
```

- **默认行为**：`AppSettings{}` ＝ `background = suspend`（**不跑后台**）、`period_ms = 0`、
  `arm = on_first_enter`（第一次进过前台才武装）。
- **该放**：只有你**真的需要切走以后继续跑**时才改。怎么判断、怎么写，§5 整节讲。
- **别放**："顺手加个 tick 保险一点"。**全仓 0 个 App 声明了非默认后台策略** ——
  包括 settings、music、album 这些看起来"应该要后台"的 App。
  多要一份后台 = 多一份周期性负载 + 多一个可能卡住 UI 的地方，白拿。
- **判据**：读完 §5 的三个问题，三个都答"是"才覆写。

### 3.8 `onForegroundTick(now_ms)` —— 你的"loop()"，以及三条硬边界

它是 `loop()` 的替代品，但比 `loop()` 苛刻：

```cpp
void StopwatchApp::onForegroundTick(std::uint32_t now_ms) {
  ui_backend_tick();                                    // ① 永远是第一句：泵一帧界面
  if (!running_) return;                                // ② 没在跑就直接返回
  const std::uint32_t elapsed = now_ms - started_ms_;   // ③ 算差值，不要"每秒加一"
  update_time_label(elapsed);                           // ④ 推给控件
}
```

**三条硬边界**（违反不会报错，只会掉帧或整机卡住）：

1. **不许阻塞**：没有 `delay()`，没有 `while` 等条件，没有同步等 I/O。
   这里是 ui 循环，5 ms 一跳（`core/config/embark_limits.h:91` 的 `ui_loop_period_ms`），
   你卡 100 ms 就是全局掉 20 帧。
2. **不许乱要内存**：没有 `new`/`malloc`，不在栈上放大数组（ui 任务栈 8 KB）。
   顺带一条：**改控件文字也会分配**（LVGL 自己 malloc），所以"读数没变就别重写"不是洁癖（§4 会看到 `last_cs_`）。
3. **不许假设被调的频率**：来了就干、干完就返回。要"每 500 ms 一次"就自己比时间戳
   （`if (now_ms - last_ >= 500) { last_ = now_ms; ... }`），**不要**用计数器猜帧率 ——
   帧率会随负载变。

> `now_ms` 是框架取好的单调递增毫秒，直接用。要主动问时间才用 `fw_->hal().time.now_ms()`
> （`hal().time` 是取墙钟的唯一入口）。

### 3.9 `onMessage(const Message& msg)` —— 收到广播

```cpp
void ClockApp::onMessage(const Message& msg) {
  if (msg.get_message_id() == BrightnessMessage::ID) {
    const auto& brightness = static_cast<const BrightnessMessage&>(msg);
    bright_percent_ = brightness.percent;     // 只更新"值"
  }
}
```

- **默认行为**：空实现 —— 你没订阅的东西永远不会来找你，所以不写就永远收不到。
- **该放**：按 `get_message_id()` 分发，**只改数据**，不改界面（界面留给下一帧
  `onForegroundTick` 统一推，这样"状态与显示"永远一致）。
- **别放**：重活（它在 ui 任务里跑 —— 你在这里卡住，整机界面就卡住）；
  用 `static_cast` 之前不判 id（转错类型 = 读到垃圾数据）。
- **两条容易意外的规则**：
  1. **你会收到自己发的消息**（发送方也在订阅表里），别惊讶，也别写成"收到就再发一遍"的死循环。
  2. **任何 App 都能收到任何广播**：广播是全局的，不是点对点。要"只让某一个 App 反应"，
     就在消息体里带目的 App 的 id，收方自己判。
- **判据**："别人改了什么，我要跟着变" → 覆写它。要"收消息就换行为"，见 §6。

### 3.10 `onBackgroundTick(now_ms)` —— 只有声明了后台策略才会被调

- **默认行为**：空实现，而且**默认一次都不会被调**（因为默认 `background = suspend`）。
  你没声明后台却在里面写代码，那代码永远不会执行 —— 编译不报错，日志也没有，最容易白写。
- **该放**：轮询传感器 / 等网络回包 / 自己维护的定时器；**必须轻量**（它跑在唯一那条 ui 任务循环里，
  你卡 100 ms，界面就卡 100 ms —— 后台 tick 不是"另一个线程"）。
- **别放**：碰界面控件（你不在前台，屏已被卸）；长耗时计算（要长耗时就申请 own task，§5.4）。
- **判据**：见 §5；先看完再决定。

### 3.11 我到底该写哪几个：一张自查表

| 你要做的事 | 覆写这些 | 千万别放错 |
| --- | --- | --- |
| 名字 / 标题 / 图标 | `name()` `title()` `icon()` | — |
| 存 `fw_`、取 HAL | `onCreate()` | 别在这里发消息、碰控件 |
| 挂屏、初始化状态 | `onEnter()`（回来时 `onResume()`） | 别只写 `onEnter` 就以为"每次进来都会跑" |
| 存状态、停节拍 | `onPause()` | 别存 `lv_obj_t*` |
| 刷界面 | `onForegroundTick()` | 别阻塞、别每帧无条件重写控件 |
| 收消息改数据 | `onMessage()` | 别在这里刷界面 |
| 切走以后还要跑 | `settings()` + `onBackgroundTick()` | 先读 §5 再动手 |
| 关机前落盘 | `onExit()` | 别把它当日常清理 |

---

## 4. 完整例子：秒表 · 必读 · 把 §2 的空壳变成真 App

这一节从空壳写到能跑，全程只有 3 个控件名要记。**代码是实测跑过的**：
点击「开始」→ 读数涨；点「暂停」→ 读数冻住；点「归零」→ 回 `00:00.00`。

### 4.1 先把界面画出来（不写一行 C）

`new_app.mjs` 给的那张屏只有一个"用 HTML 草稿画这屏"的占位。我们不进设计器，
用**文本草稿**改它 —— 这是给 AI 或给写字板准备的路子（方言细节见 [html-to-screen.md](html-to-screen.md)）。

第一步，把占位屏导成 HTML：

```bash
node tools/aidogui/html_to_aid.mjs --from-aid stopwatch --out stopwatch.html
```

你会拿到一份 30 行的 HTML，长这样（已删掉大部分样式属性）：

```html
<div id="screen">
  <div data-name="Desk" style="position: absolute; left: 0; top: 0; width: 240px; height: 320px">
    <div data-name="Btn_Back" data-action="embark_ui_open_launcher" style="...">
      <span data-name="Btn_Back_L" data-text="true" style="...">返回</span>
    </div>
    <span data-name="L_Title" data-text="true" style="...">秒表</span>
    <div data-name="Card_Placeholder" style="...">
      <span data-name="L_Time" data-text="true" style="...">00:00.00</span>
      <span data-name="L_State" data-text="true" style="...">未开始</span>
    </div>
    <div data-name="B_Start" data-clickable="true" style="...">
      <span data-name="B_Start_L" data-text="true" style="...">开始</span>
    </div>
    <div data-name="B_Reset" data-clickable="true" style="...">
      <span data-name="B_Reset_L" data-text="true" style="...">归零</span>
    </div>
  </div>
</div>
```

（这份草稿已经是我改完的样子：占位卡片里塞进了 `L_Time` 与 `L_State`，底部两个按钮
`B_Start` / `B_Reset`。你从 `--from-aid` 拿到的原始版是「卡片 + 一句提示」，
改法就是把那两个 `<span>` 换成上面这样、再加两个按钮 `<div>`。）

第二步，把草稿写回设计工程，再导出、重编：

```bash
node tools/aidogui/html_to_aid.mjs --html stopwatch.html --screen stopwatch --apply
pwsh -NoProfile -File tools/aidogui/export.ps1
cmake --build build --target embark_host_ui
```

**从这一刻起，这 5 个名字就是你的接口**（`<屏名>_<控件名>`，代码里按短名取）：

| 控件名 | 是什么 | 谁改它 |
| --- | --- | --- |
| `L_Time` | 大字号读数 `00:00.00` | `onForegroundTick`（变了才写） |
| `L_State` | 状态小字：未开始 / 计时中 / 已暂停 | 同上 |
| `B_Start` | 「开始 / 暂停」按钮本体 | 点击事件在 `B_Start` 上 |
| `B_Start_L` | 按钮**里面的文字** | 改文字要拿到这个 label，不是按钮 |
| `B_Reset` | 「归零」按钮本体 | 点击事件在 `B_Reset` 上 |

> **两个按钮里面各有一个 label**（`B_Start_L` / `B_Reset_L`）：按钮是容器，字是容器里的子控件。
> 想让按钮显示"暂停"两个字，改的是 `B_Start_L`，改 `B_Start` 没用（那是背景）。


### 4.1.1 汉字必须在字库里（否则显示成方框）

界面上的每个汉字都要在**卡上那份字体**里有字形，否则渲染出来是"豆腐块"（方框）。
字库是 `app/aidogui_ui/assets/font/ui_heiti.ttf`，构建期有缺字审计盯着：

```bash
node tools/font/audit_glyphs.mjs            # 缺字会打印 字符 + 文件:行号 并 exit 1
```

秒表这一屏用的字（秒表 / 开始 / 暂停 / 归零 / 未开始 / 计时中 / 已暂停）**都已实测在字库里**，
照抄不会踩坑。你自己加新词（尤其生僻字、公司名、英文缩写）时：

- 先跑一次上面那条命令，它是**唯一权威**（CI 里也跑，红了就合不进去）；
- 真要加字，走 `tools/font/` 那条链（`gen_font.mjs` 等），细节在
  `app/aidogui_ui/README.md` 与 [ADR 0007](../adr/0007-font-management.md)；
- **别用 emoji** —— 字库里没有，一定是方框。

> 想在设计器里拖控件而不是写 HTML 草稿？那是另一条路，步骤在
> [app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)；两条路最后都落到同一个
> `export.ps1`，代码侧看到的接口一模一样（屏名 + 控件名）。

### 4.2 头文件：13 个点里我们覆写 9 个

`app/stopwatch/stopwatch_app.h` 全文（把 §2.4 的模板改掉，注释是解释）：

```cpp
#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class StopwatchApp final : public App {
 public:
  StopwatchApp() noexcept = default;

  // —— 身份三件套 ——
  [[nodiscard]] const char* name() const override { return "stopwatch"; }
  [[nodiscard]] const char* title() const override { return "秒表"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_PLAY; }

  // —— 生命周期五件事 ——
  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

  // —— 前台节拍 ——
  void onForegroundTick(std::uint32_t now_ms) override;

  // 按钮只改"状态"，界面留给 onForegroundTick 下一帧推 —— 状态与显示永远一致。
  void toggle();  // 开始 / 暂停
  void reset();   // 归零

 private:
  static void on_screen_reloaded();  // 屏装载完：重推文案 + 重挂按钮（ADR 0015）
  void push_labels(std::uint32_t value_ms);
  static void on_start_clicked(lv_event_t* event);
  static void on_reset_clicked(lv_event_t* event);

  Framework* fw_ = nullptr;
  std::uint32_t base_ms_ = 0;            // 之前几次已经累计的毫秒
  std::uint32_t started_ms_ = 0;         // 本次开始的时刻
  std::uint32_t last_cs_ = 0xFFFFFFFFU;  // 上次推到屏上的百分秒（避免每帧重写）
  bool running_ = false;
};

}  // namespace embark::demo
```

**没有覆写的 4 个**：`settings()`（用默认 = 不跑后台，§5 会说为什么这是对的）、
`accent()`（用主题色）、`onMessage()`（不收消息）、`onBackgroundTick()`（不跑后台，写了也不会执行）。

### 4.3 实现：三段分工

全仓惯例是三段分工，你在别处看到的 App 都是这个形状：

| 段 | 钩子 | 干什么 |
| --- | --- | --- |
| 装配 | `onCreate` | 存 `fw_` + 登记"屏装载后重推值"的回调 |
| 挂屏 | `onEnter` / `onResume` | 只按屏名挂屏（屏每次都是新对象） |
| 运行 | `onForegroundTick` | 算读数 → **变化了才**写控件 |

`app/stopwatch/stopwatch_app.cpp` 全文：

```cpp
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <embark/framework.h>
#include <embark/log.h>

#include "stopwatch_app.h"

#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"
#include "ui_backend.h"
#endif

namespace embark::demo {
namespace {

#if defined(EMBARK_AIDOGUI_UI)
constexpr const char* kScreen = "stopwatch";
StopwatchApp* g_ui = nullptr;  // 静态回调没有 this，靠这个指针把实例找回来

/// 毫秒 → "分:秒.百分秒"。纯整数：这里没有浮点、也没有 std::string。
void format_elapsed(std::uint32_t ms, char* out, std::size_t out_size) {
  const std::uint32_t hundredths = ms / 10U;
  const unsigned minutes = static_cast<unsigned>(hundredths / 6000U);
  const unsigned seconds = static_cast<unsigned>((hundredths / 100U) % 60U);
  const unsigned cs = static_cast<unsigned>(hundredths % 100U);
  (void)std::snprintf(out, out_size, "%02u:%02u.%02u", minutes, seconds, cs);
}
#endif

}  // namespace

void StopwatchApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = suspend；界面 = 屏 {}）", name(), name());
#if defined(EMBARK_AIDOGUI_UI)
  g_ui = this;
  ui_backend_bind_repaint(kScreen, &StopwatchApp::on_screen_reloaded);
#endif
}

void StopwatchApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());
#endif
}

void StopwatchApp::onPause() {
  ELOG_INFO("App {} 离开前台（后台不跑，读数留在成员变量里）", name());
}

void StopwatchApp::onResume() {
  ELOG_INFO("App {} 回到前台（屏是新的，值靠重推）", name());
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());
#endif
}

void StopwatchApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void StopwatchApp::onForegroundTick(std::uint32_t now_ms) {
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_tick();                     // ① 泵一帧（永远第一句）
  if (!running_) return;                 // ② 没在跑就到此为止
  const std::uint32_t value = base_ms_ + (now_ms - started_ms_);
  if (value / 10U == last_cs_) return;   // ③ 读数没变（同一百分秒）就不重写控件
  last_cs_ = value / 10U;
  push_labels(value);
#else
  (void)now_ms;
#endif
}

void StopwatchApp::toggle() {
  if (fw_ == nullptr) return;
  const std::uint32_t now = fw_->hal().time.now_ms();
  if (running_) {
    base_ms_ += now - started_ms_;
    running_ = false;
  } else {
    started_ms_ = now;
    running_ = true;
  }
  last_cs_ = 0xFFFFFFFFU;  // 强制下一帧重推
  if (!running_) push_labels(base_ms_);
  ELOG_INFO("秒表：{}，读数 {} ms", running_ ? "开始" : "暂停", static_cast<unsigned>(base_ms_));
}

void StopwatchApp::reset() {
  base_ms_ = 0;
  started_ms_ = 0;
  running_ = false;
  last_cs_ = 0xFFFFFFFFU;
  push_labels(0);
  ELOG_INFO("秒表：归零");
}

void StopwatchApp::push_labels(std::uint32_t value_ms) {
#if defined(EMBARK_AIDOGUI_UI)
  char buf[16];
  format_elapsed(value_ms, buf, sizeof buf);
  if (lv_obj_t* label = aidogui_ui_bridge_widget(kScreen, "L_Time"); label != nullptr) {
    lv_label_set_text(label, buf);
  }
  const char* state = running_ ? "计时中" : (value_ms > 0 ? "已暂停" : "未开始");
  if (lv_obj_t* label = aidogui_ui_bridge_widget(kScreen, "L_State"); label != nullptr) {
    lv_label_set_text(label, state);
  }
  if (lv_obj_t* button = aidogui_ui_bridge_widget(kScreen, "B_Start_L"); button != nullptr) {
    lv_label_set_text(button, running_ ? "暂停" : "开始");
  }
#else
  (void)value_ms;
#endif
}

#if defined(EMBARK_AIDOGUI_UI)
void StopwatchApp::on_screen_reloaded() {
  if (g_ui == nullptr) return;
  // ① 值重推：新控件上是设计器里的初值
  g_ui->push_labels(g_ui->running_
                        ? g_ui->fw_->hal().time.now_ms() - g_ui->started_ms_ + g_ui->base_ms_
                        : g_ui->base_ms_);
  // ② 事件重挂：旧按钮已经跟着旧屏销毁了，这几个指针全是新的
  if (lv_obj_t* button = aidogui_ui_bridge_widget(kScreen, "B_Start"); button != nullptr) {
    lv_obj_add_event_cb(button, &StopwatchApp::on_start_clicked, LV_EVENT_CLICKED, g_ui);
  }
  if (lv_obj_t* button = aidogui_ui_bridge_widget(kScreen, "B_Reset"); button != nullptr) {
    lv_obj_add_event_cb(button, &StopwatchApp::on_reset_clicked, LV_EVENT_CLICKED, g_ui);
  }
}

void StopwatchApp::on_start_clicked(lv_event_t* event) {
  auto* self = static_cast<StopwatchApp*>(lv_event_get_user_data(event));
  if (self != nullptr) self->toggle();
}

void StopwatchApp::on_reset_clicked(lv_event_t* event) {
  auto* self = static_cast<StopwatchApp*>(lv_event_get_user_data(event));
  if (self != nullptr) self->reset();
}
#endif  // EMBARK_AIDOGUI_UI

}  // namespace embark::demo
```

### 4.4 这份代码里，每个"奇怪"写法都在躲一个坑

| 写法 | 不这么写会怎样 |
| --- | --- |
| `if (!running_) return;` 放在 `ui_backend_tick()` **之后** | 放在前面 → 暂停时界面不再刷新，按钮按下去没反应（因为 tick 才是"把这帧画出来"） |
| `if (value / 10U == last_cs_) return;` | 每帧都 `lv_label_set_text` → 每秒 200 次堆分配，跑一分钟就开始掉帧 |
| 读数用 `base_ms_ + (now - started_ms_)`，不用 `counter++` | 帧率一变读数就飘；而 `now_ms` 是框架给的单调时钟，天然准确 |
| 按钮回调只改 `running_`/`base_ms_`，不碰控件 | 回调里改控件 → 与 `onForegroundTick` 抢着写同一个 label，显示与状态会短暂不一致 |
| 控件全部走 `aidogui_ui_bridge_widget(kScreen, "名字")` | 存 `lv_obj_t*` 到成员里 → 切走再回来，那个指针已经指向已销毁的对象（崩溃或乱码） |
| 事件在 `on_screen_reloaded()` 里挂 | 只在 `onCreate` 挂一次 → 第二次进这个 App，按钮**全部失灵**（旧按钮已经销毁） |
| `format_elapsed` 用整数除法和 `snprintf` | 用浮点 → 嵌入式没开浮点格式化，`%.2f` 打出来是乱的 |

### 4.5 验收：点三下，看四个读数

```bash
./build/host/embark_host_ui.exe --delay 40
```

launcher → 翻到最后一页 → 点「秒表」→ 依次点 **开始**（等 2 秒）→ **暂停** →
再点 **开始**（等 1 秒）→ **归零**。屏幕上的四个读数依次是：

| 时刻 | `L_Time` | `L_State` | `B_Start_L` |
| --- | --- | --- | --- |
| 刚进来 | `00:00.00` | 未开始 | 开始 |
| 跑着 | 数字在涨 | 计时中 | 暂停 |
| 暂停后 | **冻住不动** | 已暂停 | 开始 |
| 归零后 | `00:00.00` | 未开始 | 开始 |

再补一个"切走再回来"的验收（这条最能暴露野指针问题）：跑着的时候点返回回 launcher，
等两秒再进秒表 —— **读数应该继续涨**（时间一直在走，`started_ms_` 还在）。
如果回来是 `00:00.00`，说明你在 `onPause` 里把 `started_ms_` 清掉了；
如果界面一片空白，说明 `onResume` 里没挂屏。

> 想验证"暂停时真的冻住了"而不是"界面卡了不刷新"：看 `B_Start_L` 有没有从「暂停」变回「开始」——
> 变了 = 界面在刷新，只是读数不再变。

---

## 5. 后台策略：先问三个问题，再决定要不要 · 需要时读

这节最容易被想复杂。结论先说：**大部分 App 应该什么都不做**（保持默认 `suspend`，也就是不跑后台）。
全仓 6 个 App —— 包括看起来最"必须后台"的时钟、音乐、相册 —— **声明非默认后台策略的一个都没有**。

### 5.1 后台策略是什么

`settings()` 返回一个 `AppSettings`（`core/include/embark/app.h:55-61`），三个字段：

```cpp
struct AppSettings {
  BackgroundPolicy background = BackgroundPolicy::suspend;  // 后台怎么跑
  std::uint32_t period_ms = 0;                              // tick 的周期（0 = 等于 suspend）
  std::uint16_t task_stack_words = 0;                       // own_task 的栈（单位：字，0 = 用默认 512）
  std::uint16_t task_priority = 0;                          // own_task 的优先级（0 = 用默认）
  ArmPolicy arm = ArmPolicy::on_first_enter;                // 什么时候"武装"后台
};
```

| `background` | 行为 | 谁在用 |
| --- | --- | --- |
| `suspend`（默认） | 离开前台后**完全不跑**，`onBackgroundTick` 一次都不会被调 | 全仓所有 App |
| `tick` | 按 `period_ms` 周期调你的 `onBackgroundTick(now_ms)`，跑在**唯一的 ui 任务**里 | — |
| `own_task` | 给你开一条**自己的 FreeRTOS 任务**（真的另一个线程） | — |

`period_ms = 0` 时 `tick` 等价于 `suspend` —— 写了 `tick` 但忘了填周期，效果就是"什么都没发生"，
而且它不报错。这是本节第一个坑。

### 5.2 三个问题：三个都答"是"才动手

**问题一：离开前台以后，这件事不推进就会错吗？**

- "会错"的例子：到点要响的闹钟、正在上传不能断的传输、要记录全程的传感器采样。
- "不会错"的例子：时钟（时间是单调时钟，回来看 `now_ms` 就算出来了）、秒表（返回时算差值）、
  待办列表（数据在存储里，回来重读）。

**问题二：能不能"回来再补偿"？**

99% 的情况能。框架的时间是**单调递增**的（`now_ms` 只增不减，不受时区/校时影响），
所以"上次是什么时候"这件事永远可以事后算：

```cpp
// 不需要后台：切走时记时间戳，回来算差值
void MyApp::onPause() { paused_at_ms_ = fw_->hal().time.now_ms(); }
void MyApp::onResume() {
  const std::uint32_t away = fw_->hal().time.now_ms() - paused_at_ms_;
  ELOG_INFO("离开了 {} ms", away);   // 期间发生的事，回来一次性补上
}
```

**问题三：如果真要做，是"周期看一眼"还是"必须自己一条执行流"？**

| 要干的事 | 选 |
| --- | --- |
| 每 200 ms 读一次传感器、看一眼标志位 | `tick` |
| 等一个网络回包、跑一个几百毫秒的计算、需要阻塞等待 | `own_task`（但先看 5.4 的三条纪律） |
| "到点提醒我"这种跟你在不在前台无关的 | `tick` + `arm = at_boot` |

### 5.3 `tick` 怎么写（以及它为什么不是"另一个线程"）

```cpp
// 头文件
[[nodiscard]] AppSettings settings() const override {
  AppSettings s{};
  s.background = BackgroundPolicy::tick;
  s.period_ms = 500;      // 每 500 ms 一次
  s.arm = ArmPolicy::on_first_enter;   // 进过一次前台才武装（默认）
  return s;
}
void onBackgroundTick(std::uint32_t now_ms) override;   // 实现里干活
```

**最关键的一句：`tick` 跑在唯一那条 ui 任务循环里**（`core/src/embark/framework.cpp:193-198`，
每 5 ms 的循环里顺带调你）。所以：

- 你在这里卡 100 ms，**整机界面就卡 100 ms**。它不是"后台线程"，只是"后台时段的回调"。
- 你**不能**碰界面控件（你已经不在前台，屏被卸了），也**不需要** `ui_backend_tick()`。
- 想在后台时段干重活 → 那是 `own_task`，不是这里。

`arm` 是什么：`on_first_enter`（默认）= 只有你**进过一次前台**之后，后台才会被武装；
`at_boot` = boot 时就武装，哪怕用户从没打开过这个 App —— 闹钟类才需要，
代价是开机就多一份周期负载。

容量上限：`max_background_timers = 8`（`core/config/embark_limits.h:21`）。超了就是"武装失败"，
框架会计一次数（`Framework::arm_failures()`），日志里能看到。

### 5.4 `own_task`：真的要一条自己的任务时

先在平台层看两个上限：`max_own_tasks = 2`（同时最多 2 条）、默认栈 `own_task_stack_words = 512`
（字 × 4 字节 = **4 KB**，`core/config/embark_limits.h:71/79`）。**只有 2 个槽**，这是稀缺资源。

起始方式只有一句：

```cpp
void MyApp::onEnter() {
  ui_backend_load_for_app(name());

  // ① 先反查自己的编号（不在注册表里 → invalid_app_id）
  const AppId id = fw_->apps().id_of(*this);
  if (id == invalid_app_id) {
    ELOG_ERROR("{} 不在 App 表里（忘了 EMBARK_APP_TABLE？）", name());
    return;
  }

  // ② 申请一条自己的任务；没返回值，成功是 Error::none
  const Error spawned = fw_->spawn_own_task(id);
  if (spawned != Error::none) {
    ELOG_WARN("申请 own task 失败：{}", spawned);   // 打出 Error::no_space 这样的名字
  }
}
```

`AppId` / `invalid_app_id` 在 `core/include/embark/message.h:44-47`，`Error` 在 `core/include/embark/error.h`
（枚举值直接能用 `{}` 打印，这是 efmt 的枚举推导能力，见 §7）。

错误码（`core/include/embark/framework.h:141-143`）：`not_found`（编号无效）/ `not_ready`（还没 boot 或已 shutdown）/
`unsupported`（平台没给 spawner，或这个 App 不是 `own_task` 策略）/ `no_space`（2 个槽都占着，**不排队**）/
`busy`（这个 App 已经有一条了）。

三条纪律（`docs/concepts/app-lifecycle.md:456-465`）：

1. **回收只能由持有者做**：框架在 ui 任务里替你 `reap_finished_own_tasks()`，你别自己去放飞。
2. **池满即 `no_space`，不排队**：申请不到就降级（比如"这次就不后台采了"），别死循环重试。
3. **别在自己的任务里等结果**：任务之间用消息通信，不要加锁同步等待。

还有两条硬规矩（比上面三条更容易踩）：

- **在自己任务里绝对不要碰 LVGL**（只有 ui 任务能碰，见 §10 红线）。要更新界面就把数据丢回来：
  `fw_->post(SomeMessage{...})`（跨任务发消息**只能**用 `post`，不能用 `publish`）。
- **别在自己的任务里读 `lv_obj_t*`，也别假设 App 还在前台** —— 任务比"前台/后台"活得久。

> **别自己算 id**：`AppRegistry::id_of(const App&)`（`core/include/embark/app_registry.h:65-72`）
> 是唯一的反查入口，它按指针比对，返回 `invalid_app_id` 表示不在表里。

**最后一劝**：先按 5.2 的三个问题走一遍。需要 `own_task` 的场景在这个演示工程里**一个都没有**；
你要用，先能回答"为什么 `tick` 不够"。

---

## 6. 状态机与消息：什么时候该用 · 需要时读

### 6.1 先判一件事：你到底需不需要状态机

| 你的情况 | 用什么 |
| --- | --- |
| 只有"开着/关着"两态 | **一个 `bool`**。秒表（§4）就是这样：`running_` 一个 bool 撑起全部逻辑 |
| 状态 ≥3 个，且"同一件事在不同状态下反应不同" | 状态机（本节 6.3） |
| 状态 ≥3 个，但每个状态只影响一小段 if | `enum` + `switch` 就够，别上 fsm |
| 别人的改动要影响我（亮度变了、音量变了） | 消息（本节 6.2） |

**一条实测结论**：全仓（`app/`、`core/src/`、`host/`、`device/`、`core/examples/`）**没有一个 App 用 `etl::fsm`**。
所以"用 fsm"是可选能力，不是必修课。看不懂 6.3 可以先跳过，等你真有三态以上再回来。

### 6.2 消息：从发到收，四步

消息是这个框架里 App 之间**唯一**的通信方式（App 之间不许互相 include，见 §10）。

**第一步：定义一条消息**（`app/common/app_messages.h` 是现成的公共消息区）

```cpp
#include <embark/message.h>

// id 是 0x01~0xFF 的一个数；0xFE 框架保留（跨任务信封），你从 0x20 往上挑没人的号
struct BrightnessMessage : public embark::MessageT<0x21> {
  explicit BrightnessMessage(std::uint8_t p = 100) noexcept : percent(p) {}
  std::uint8_t percent;
};
```

`MessageT<0x21>` 会给你一个静态常量 `BrightnessMessage::ID`（值就是 0x21），收方拿它比对。

**第二步：发**

```cpp
// 在 App 的任意钩子里（除了 onCreate —— 那时总线还没装配）
fw_->publish(BrightnessMessage{70});
```

- `publish` **只能在框架线程**（也就是你的钩子里）调。
- **没人订阅时会打一条 WARN 并计数**（`core/src/embark/bus.cpp:66-70`）—— 不是崩溃，但看到这条 WARN
  说明"你发了，但这个构建里没人听"。
- 广播是**全局**的：所有 App 都会收到，不是点对点。

**第三步：收**

```cpp
void ClockApp::onMessage(const Message& msg) {
  if (msg.get_message_id() != BrightnessMessage::ID) return;   // 先判 id，再转型
  const auto& m = static_cast<const BrightnessMessage&>(msg);
  bright_percent_ = m.percent;    // 只改数据
  // 界面不改：下一帧 onForegroundTick 统一推，状态与显示永远一致
}
```

**第四步：确认它真的会到**

三条容易意外的规则，写之前先看：

1. **你会收到自己发的消息**（发送方也在订阅表里，`core/src/embark/bus.cpp:74`）。
   所以别写"收到 X 就再发 X"，那会无限循环。
2. **`onCreate` 里发消息没人收** —— 总线在 boot 第 3 步才装配，而 `onCreate` 是第 1 步
   （`core/src/embark/framework.cpp:46-54`，注释原文「装配消息请放到 onEnter 之后」）。
3. **App 不能自己 subscribe** —— `fw_->bus()` 返回的是 `const Bus&`，只读。
   订阅是框架在 boot 时替你做的，你只管实现 `onMessage`。

### 6.3 状态机：`etl::fsm` 的最小可用骨架

真需要时（比如"关 / 预热 / 恒温 / 故障"四态，每个状态对按键和定时器的反应都不同），
用仓库已经带的 `etl::fsm`（`third_party/etl`，头文件路径 `<middleware/etl/fsm.h>`）。
下面这份骨架**实测编译并跑通**（跑出来的就是 `idle → running → paused → idle` 四个读数，
暂停时读数冻住、恢复后接着涨）。

**必须先理解它的分工**：状态机上"有几种状态"，状态类里"某个事件在这个状态下该跳去哪"，
数据（累计毫秒这种）放"状态机本体"（context）。状态类会反复进出，**别把数据放状态类里**。

```cpp
#include <cstdint>

#include <middleware/etl/fsm.h>

#include <embark/message.h>

using embark::MessageT;

// ① 事件 = 消息。id 从 0x30 起挑没人用的号（0xFE 框架保留给跨任务信封）
struct StartMessage : public MessageT<0x30> {
  constexpr StartMessage(std::uint32_t t = 0) noexcept : now_ms(t) {}
  std::uint32_t now_ms = 0;   // 按下那一刻的时刻，由 App 填
};
struct PauseMessage : public MessageT<0x31> {
  constexpr PauseMessage(std::uint32_t t = 0) noexcept : now_ms(t) {}
  std::uint32_t now_ms = 0;
};
struct ResetMessage : public MessageT<0x32> {
  constexpr ResetMessage() noexcept = default;
};

// ② 状态 id：必须 0..N-1 且按声明顺序（fsm_state_pack 有编译期检查）
enum StateId : etl::fsm_state_id_t { kIdle = 0, kRunning = 1, kPaused = 2 };

class StopwatchFsm;   // ③ 先声明状态机类型名（状态类要用到它）

// ④ 状态类：只声明"处理哪些事件"，函数体放最后
class StateIdle : public etl::fsm_state<StopwatchFsm, StateIdle, kIdle, StartMessage, ResetMessage> {
 public:
  etl::fsm_state_id_t on_event(const StartMessage& msg);
  etl::fsm_state_id_t on_event(const ResetMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

class StateRunning : public etl::fsm_state<StopwatchFsm, StateRunning, kRunning, StartMessage,
                                           PauseMessage, ResetMessage> {
 public:
  etl::fsm_state_id_t on_event(const StartMessage& msg);
  etl::fsm_state_id_t on_event(const PauseMessage& msg);
  etl::fsm_state_id_t on_event(const ResetMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

class StatePaused : public etl::fsm_state<StopwatchFsm, StatePaused, kPaused, StartMessage,
                                          PauseMessage, ResetMessage> {
 public:
  etl::fsm_state_id_t on_event(const StartMessage& msg);
  etl::fsm_state_id_t on_event(const PauseMessage& msg);
  etl::fsm_state_id_t on_event(const ResetMessage& msg);
  etl::fsm_state_id_t on_event_unknown(const etl::imessage&);
  etl::fsm_state_id_t on_enter_state();
};

// ⑤ 状态机本体（context）：数据全在这，零堆（fsm_state_pack 是静态存储）
class StopwatchFsm final : public etl::fsm {
 public:
  StopwatchFsm() noexcept : etl::fsm(kIdle) { set_states(states_); }

  void start_at(std::uint32_t now_ms) noexcept {
    started_ms_ = now_ms;
    running_ = true;
  }
  void pause_at(std::uint32_t now_ms) noexcept {
    if (running_) base_ms_ += now_ms - started_ms_;
    running_ = false;
  }
  void clear() noexcept {
    base_ms_ = 0;
    running_ = false;
  }
  [[nodiscard]] std::uint32_t value(std::uint32_t now_ms) const noexcept {
    return running_ ? base_ms_ + (now_ms - started_ms_) : base_ms_;
  }

 private:
  etl::fsm_state_pack<StateIdle, StateRunning, StatePaused> states_;
  std::uint32_t base_ms_ = 0;
  std::uint32_t started_ms_ = 0;
  bool running_ = false;
};

// ⑥ 函数体最后写：get_fsm_context() 需要 StopwatchFsm 的完整定义
etl::fsm_state_id_t StateIdle::on_event(const StartMessage& msg) {
  get_fsm_context().start_at(msg.now_ms);
  return kRunning;                       // 返回值 = 下一个状态
}
etl::fsm_state_id_t StateIdle::on_event(const ResetMessage&) { return No_State_Change; }
etl::fsm_state_id_t StateIdle::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StateIdle::on_enter_state() { return No_State_Change; }

etl::fsm_state_id_t StateRunning::on_event(const StartMessage&) { return No_State_Change; }
etl::fsm_state_id_t StateRunning::on_event(const PauseMessage& msg) {
  get_fsm_context().pause_at(msg.now_ms);
  return kPaused;
}
etl::fsm_state_id_t StateRunning::on_event(const ResetMessage&) {
  get_fsm_context().clear();
  return kIdle;
}
etl::fsm_state_id_t StateRunning::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StateRunning::on_enter_state() { return No_State_Change; }

etl::fsm_state_id_t StatePaused::on_event(const StartMessage& msg) {
  get_fsm_context().start_at(msg.now_ms);
  return kRunning;
}
etl::fsm_state_id_t StatePaused::on_event(const PauseMessage&) { return No_State_Change; }
etl::fsm_state_id_t StatePaused::on_event(const ResetMessage&) {
  get_fsm_context().clear();
  return kIdle;
}
etl::fsm_state_id_t StatePaused::on_event_unknown(const etl::imessage&) { return No_State_Change; }
etl::fsm_state_id_t StatePaused::on_enter_state() { return No_State_Change; }
```

怎么接进 App（三个钩子各一句）：

```cpp
void StopwatchApp::onCreate(Framework& fw) { fw_ = &fw; fsm_.start(); }   // ① 起机（只一次）
void StopwatchApp::onMessage(const Message& msg) { fsm_.receive(msg); }   // ② 消息全喂给它
// ③ 界面照旧在 onForegroundTick 里按 fsm_.value(now_ms) 推；要读状态用 fsm_.get_state_id()
```

**四条规矩**（每条都对应一个会让你查一小时的坑）：

1. **`on_event` 返回下一个状态 id**；`No_State_Change` = "本状态不管这个事件"。返回值漏写不会警告。
2. **数据放 context，别放状态类** —— 状态类反复进出，累计值放进去会莫名其妙归零。
3. **函数体写在 context 之后** —— `get_fsm_context()` 需要完整类型，写在前面报
   `invalid use of incomplete type`。
4. **状态 id 从 0 连续编号、按声明顺序** —— `fsm_state_pack` 会静态断言这个，跳号编不过。
5. **别把 `lv_obj_t*` 存进 context** —— 界面每次装载都是新对象，状态机比界面活得久。

> 完整可运行的 fsm 示例在 `docs/guides/state-machine-and-messages.md`
> （"LampFsm"：一个灯的 on/off/调光），那份是文档里的例子，不是仓库里的 App。

---

## 7. efmt：格式化字符串 · 需要时读

efmt 是这个仓库的"printf 替代品"。你**不用特意学它也能把 App 写完**（§4 的秒表用的是 `snprintf`），
但它有两件事值得会：

1. **`ELOG_*` 系列日志宏的 `{}` 占位符就是 efmt**（§8）—— 你已经在用它了。
2. 想往 LVGL 的 label 里塞"拼好的字符串"时，`format_to` 比 `snprintf` 安全（有边界检查、类型检查）。

头文件：`#include <middleware/efmt/core/format.hpp>`，命名空间 `e_fmt`。

### 7.1 你可能已经见过它的产出

开机日志里这一段，就是 efmt 把 `AppSettings` 整个结构体打出来的（`{:#}` 是"带字段名"的格式）：

```plain
[info] [framework.cpp:61 boot] App clock 后台配置 embark::AppSettings {
  background = embark::BackgroundPolicy::suspend,
  period_ms = 0,
  task_stack_words = 0,
  task_priority = 0,
  arm = embark::ArmPolicy::on_first_enter
}
```

**这条日志就是你在 §5 里改后台策略后的唯一验收手段**：改完 `settings()` 跑一次，
看这里是不是你想要的三个值。改错了不用猜，它自己会说。

### 7.2 三个接口，你只需要第一个

| 接口 | 长什么样 | 用在哪 |
| --- | --- | --- |
| `format_to` | `std::size_t format_to(char* buf, std::size_t size, const char* fmt, Args... args)` | **主力**：写进你的栈上 `char buf[N]` |
| `formatted_size` | `std::size_t formatted_size(fmt, args...)` | 先问"要几个字节"，再决定缓冲多大 |
| `format` | 返回 `std::string` | **只在宿主（PC）可用**，嵌入式默认不参与编译 |

`format_to` 的返回值是"**完整输出需要多少字节**"（跟 `snprintf` 一个语义）：

```cpp
char buf[16];
const std::size_t needed = e_fmt::format_to(buf, sizeof buf, "{:02}:{:02}.{:02}", m, s, cs);
// needed = 8，buf = "00:00.65"
// 如果 needed >= sizeof buf，说明被截断了 —— 这是你能检测到的截断，snprintf 也行但要自己判
```

仓库里 `embark::error_text()`（`core/include/embark/error.h:56-60`）就是这个套路：

```cpp
char text[24];
std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
// 打出：HAL 初始化失败：Error::io_failure
```

### 7.3 格式串速查（够用版）

| 写法 | 意思 | 例子（输出） |
| --- | --- | --- |
| `{}` | 下一个参数，自动编号 | `"{} {}"`, `1, 2` → `1 2` |
| `{0}` `{1}` | 手动指定第几个参数（从 0 起） | `"{1}-{0}"`, `5, 9` → `9-5` |
| `{:02}` | 宽度 2、左边补 0 | `"{:02}"`, `7` → `07` |
| `{:>6}` | 宽度 6、右对齐（`<` 左、`^` 居中） | `"{:>4}"`, `"ab"` → `"  ab"` |
| `{:x}` `{:#x}` `{:X}` | 十六进制（`#` 带 `0x`） | `"{:#x}"`, `255` → `0xff` |
| `{:.2f}` | 两位小数（浮点在嵌入式也**是**开着的） | `"{:.2f}"`, `3.14159` → `3.14` |
| `{{ }}` | 打一个花括号本身 | `"{{{}}}"`, `1` → `{1}` |
| `{:#}` | **结构体**：带字段名打出来（见 7.1） | — |

### 7.4 三条嵌入式限制（踩了才知道）

| 限制 | 具体规则 | 后果 |
| --- | --- | --- |
| **参数最多 8 个** | 宿主 16、设备 8（`EFMT_MAX_FORMAT_ARGS`） | 超过 → **编译报错**（不是运行期） |
| **没有 `std::string`** | `EFMT_ENABLE_DYNAMIC_STRING` 跟随"是否宿主" | 用了 `format()` → 编不过（真机目标） |
| **`print`/`println` 缓冲 256 字节** | `EFMT_PRINT_BUFFER_SIZE` | 超长被截断；**写日志请用 `ELOG_*`，别用 `print`** |

还有一条不是限制但会吓你一跳：**颜色转义只在宿主有**（`EFMT_ENABLE_ANSI_STYLES` 跟随宿主）。
所以宿主控制台里日志是彩色的（`[info]` 蓝色），真机串口是纯文本 —— 同一份代码，两种观感。

### 7.5 什么时候用 efmt，什么时候 `snprintf` 就够

- **就是几个整数、格式固定**（秒表 `00:00.00`）→ `snprintf` 可以，别为了用而用。
- **要插字符串、要带名字打印结构体、参数类型可能变**→ `format_to`（`snprintf` 的 `%s` 传错类型不报错，
  而 `format_to` 会编不过，这个差别救过很多次）。
- **不要两套混在同一条日志里** —— 全用 `{}` 最省心。

### 7.6 让你的 enum / struct 也能被 `{}` 打印

框架自己的 `Error`、`BackgroundPolicy` 都能直接打（7.1 那段就是证据），靠的是两个宏，写在**声明处**：

```cpp
#include <middleware/efmt/core/format.hpp>   // 推导宏已经被它带进来了，不用再单独 include

// enum：包在声明外面
E_FMT_DERIVE_ENUM(enum class Mode : std::uint8_t { kIdle = 0, kTiming = 1 });

// struct：同样包在声明外面（注意末尾分号）
E_FMT_DERIVE(struct Reading {
  std::uint32_t value = 0;
  bool valid = false;
});
```

之后 `ELOG_INFO("mode={} reading={:#}", mode, reading);` 就能打出 `mode=Mode::kTiming reading=Reading { value = 42, valid = true }`。
**给初学者的话**：不写这两个宏完全没问题，只是打出来是 `<unsupported>` 之类的东西。

---

## 8. elog：日志 · 需要时读（但强烈建议读完，它只占 1 分钟）

日志是你唯一能在真机上看清"刚才发生了什么"的手段。仓库已经替你把 logger 装好了
（`host/host_context.cpp:22-25`、`device/src/esp32_hal.cpp:25-28`），
你的 App 里**只要 include 一个头就能打**：

```cpp
#include <embark/log.h>     // App 里要打日志，只加这一行

ELOG_INFO("App {} onCreate", name());
ELOG_WARN("缺屏：{}", name());
ELOG_ERROR("写盘失败：{}", error);
```

### 8.1 一行日志长什么样

```plain
[info] [clock_app.cpp:158 onCreate] App clock onCreate（不申请后台；界面 = 生成的 clock 屏卡片流）
   ↑       ↑        ↑      ↑              ↑
  级别   文件名   行号   函数名          你自己写的内容
```

（宿主上还带 ANSI 颜色，上面把转义去掉了。真机串口没有颜色。）

### 8.2 三个宏够用，另外三个别用

| 宏 | 什么时候用 | 仓内用量 |
| --- | --- | --- |
| `ELOG_INFO` | 生命周期、状态变化、"我走到这了" | 220 处 |
| `ELOG_WARN` | 能继续、但不对（缺屏、退化、被截断） | 99 处 |
| `ELOG_ERROR` | 失败了（写盘失败、初始化失败） | 75 处 |
| `ELOG_TRACE` / `ELOG_DEBUG` / `ELOG_CRITICAL` | 存在，但**全仓 0 处用** | 0 |

一条经验：**每个钩子进来打一行 `ELOG_INFO("App {} onXxx", name())`**，代价是一行代码，
收益是"App 到底有没有被执行到"这件事永远不用猜 —— 全仓 App 都这么写，照抄即可。

### 8.3 铁律：整行超过 384 字节，**这条日志会整条消失**

elog 单条记录的缓冲是 384 字节（`ELOG_MAX_RECORD_SIZE`，`third_party/efmt-elog/elog/elog.hpp:65`）。
放不下的时候它**不是截断，是整条丢掉**（`elog.hpp:211-220`）—— 你在日志里看不到任何痕迹，
会以为"这行代码没执行"。所以：

- **别打大块数据**：不要 `ELOG_INFO("{}", whole_array)`，不要打长 JSON、长 HTML。
  要打就用循环打前几条（`for (i < 4 && i < n)`）。
- **前缀已经吃掉几十字节**（`[info] [file.cpp:123 func]`），你的正文实际可用 300 字节出头。
- 二进制/中文混排要注意：一个汉字 3 字节。

### 8.4 三条使用纪律

1. **不要在 `onForegroundTick` 里打日志**。那是每帧一次（一秒几百条），会把串口刷爆、
   把界面拖慢。要观测每帧的值就"变了才打"或计数后每 N 帧打一次。
2. **日志里带上文**：`ELOG_WARN("缺屏 {}（下一步：在设计器建同名屏并导出）", name())`
   比 `ELOG_WARN("error")` 有用一百倍。这个仓库的日志风格就是"给人看的"，跟着写。
3. **级别可以在安装 logger 时调**（`embark::install_logger(name, binder, e_log::level::warn)`，
   `core/include/embark/log.h:78-79`），真机上嫌吵就调到 `warn` —— 这是改平台层的事，App 不用管。

### 8.5 日志里最值得你盯的三条

| 你想知道 | 去日志里找 |
| --- | --- |
| 我的 App 被框架看见了吗 | `App <名字> 后台配置`（boot 期每个 App 一行） |
| 我的屏被找到了吗 | `AiDoGUI 点击绑定：屏 xxx 的 Card_xxx → App xxx` / 或那条"没有屏 xxx"的 WARN |
| 钩子的调用顺序对不对 | `onCreate` / `进入前台（enter n, resume m）` / `离开前台` 这几行按时间排 |

---

## 9. etl：会用三个就够 · 需要时读

etl 是个"嵌入式版 STL"（`third_party/etl`）。**这个仓库禁 `new`、禁异常、禁 `std::string`**，
所以 STL 里那些会分配的东西都不能用 —— etl 就是来补这个洞的。头文件都走
`<middleware/etl/xxx.h>`（构建时把 `third_party` 映射成了 `middleware`）。

你真正会用到的只有三个：**`etl::array`（定容数组）、`etl::expected`（错误返回）、`etl::string_view`（只读字符串）**。

### 9.1 `etl::array<T, N>` —— 不分配的数组

```cpp
#include <middleware/etl/array.h>

etl::array<std::uint32_t, 8> marks{};   // 容量 8，就在对象里，不碰堆
std::size_t used = 0;

void add_mark(std::uint32_t t) {
  if (used >= marks.size()) {          // 满了：自己判，它不会替你扩容
    ELOG_WARN("标记满了（{}/{}）", used, marks.size());
    return;
  }
  marks[used++] = t;
}
```

**跟 `std::vector` 的区别**：容量编译期定死，**没有 `push_back` 自动扩容**。
所以要自己维护 `used`（或者用 `etl::array` + 一个 `size` 字段封成自己的小结构）。
"满了怎么办"永远是你来回答：丢最旧的、拒绝新的、还是报警 —— 别让它悄悄覆盖。

### 9.2 `etl::expected<T, Error>` —— 替代异常的"可能失败"

这个仓库 **`throw` 直接编不过**（`-fno-exceptions`），所以"失败了怎么告诉调用者"只有一条路：
返回值里带上错误码。

```cpp
#include <middleware/etl/expected.h>
#include <embark/error.h>

// 返回"值"或者"错误码"，二选一
[[nodiscard]] etl::expected<std::uint32_t, Error> read_counter() noexcept {
  if (!ready_) return embark::unexpected(Error::not_ready);
  return counter_;                              // 成功：直接 return 值
}

// 调用方：先判、再取
const auto result = read_counter();
if (!result) {
  ELOG_WARN("读计数失败：{}", result.error());    // 打出 Error::not_ready
  return;
}
const std::uint32_t value = *result;             // 或 result.value()
```

- 造失败值统一用 `embark::unexpected(Error::xxx)`（`core/include/embark/error.h:45-47`），
  别手写 `etl::unexpected<Error>(...)`。
- 函数标记 `[[nodiscard]]`，忘了判返回值会有编译警告 —— 这是好事。
- 错误的**名字**能直接打出来（`Error::no_space`），因为 `Error` 用了 `E_FMT_DERIVE_ENUM`（§7.6）。

### 9.3 `etl::string_view` —— 借一段字符串，不复制

```cpp
#include <middleware/etl/string_view.h>

// 只读、零拷贝：不拥有这段内存，所以别让被指的东西先死
void show_city(etl::string_view city) {
  ELOG_INFO("城市：{}", city.data());   // 注意：日志占位符要 data()，不是 string_view 本身
}
show_city("北京");                       // 从字符串字面量构造，不分配
```

**致命注意**：`string_view` 不拥有内存。指着局部 `char buf[]` 的 `string_view` 一旦返回出去就是悬空指针。
仓库里的用法都是"函数参数 / 成员指向静态常量"，照这个范围用就安全。

### 9.4 知道有这回事就行（别急着用）

| 东西 | 一句话 |
| --- | --- |
| `etl::span<T>` | 数组的"视图"（指针 + 长度），传数组给函数用它，比传裸指针安全 |
| `etl::mutex` / `etl::lock_guard` | 跨任务互斥；**App 层几乎不需要**（App 的钩子都在 ui 任务里，天然串行） |
| `etl::imessage` / `etl::message_bus` | 框架的消息基础设施，你只碰 `MessageT<Id>` 和 `publish`（§6） |
| `etl::timer` / `etl::callback_timer` | 框架的后台定时器底座；**App 别自己建**，用 `settings()` 的 `tick`（§5） |

**给初学者的一句话总结**：`etl::array` 用来存"数量有限的一串东西"，`etl::expected` 用来"返回错误"，
其余等你真需要时再查 `third_party/etl/include/etl/`。

---

## 10. 红线：这几件事不要做 · 必读

上面各节零散提过，这里集中成一张表。**前六条是硬红线**（违反了会崩、会花屏、会莫名其妙卡）；
后三条是软约束（不崩，但会让后面的人（包括三个月后的你）骂人）。

### 10.1 硬红线

| # | 不要做 | 为什么 | 正确做法 |
| --- | --- | --- | --- |
| 1 | **`new` / `delete` / `malloc` / `free`** | 零堆纪律（`docs/adr/0003-no-heap-no-exceptions.md`）。注意：**没有任何运行期拦截**，宿主上照样能跑，上真机可能因为碎片在几小时后崩 | 成员变量、静态数组、`etl::array`；要"动态"就用定容池 |
| 2 | **从自己的 own task 里碰 LVGL** | LVGL 不是线程安全的，只有 ui 任务能碰 | 把数据 `fw_->post(MyMessage{...})` 丢回 ui 任务，在 `onMessage` 里改 |
| 3 | **在钩子里阻塞**（`while` 等条件、`sleep`、同步等 I/O） | `onForegroundTick` / `onBackgroundTick` / `onMessage` **全部跑在唯一的 ui 任务里**，你卡住就是整机界面卡住（帧间隔 5 ms） | 用状态机"下一帧再看一眼"；真需要阻塞就申请 own task（§5.4） |
| 4 | **App 之间 `#include` 对方的头** | `app/CMakeLists.txt:22` 明令禁止：App 之间只能通过消息通信，互相 include 会变成一团泥 | 发消息（§6），或把公共的东西放进 `app/common/` |
| 5 | **手改 `app/aidogui_ui/export/`** | 那是生成物，`export.ps1` 会**整个目录删掉重生**，你的修改一次就没了 | 改设计器（或 HTML 草稿）再导出；误改了用 `git checkout -- app/aidogui_ui/export` |
| 6 | **用 `CHANGE_SCREEN` 做导航** | `docs/adr/0011`：跨 App 跳转必须走设计器的"调用函数"动作（生成 `embark_ui_open_<App名>()`），手写换屏会绕开前台的钩子，界面和状态对不上 | 在设计器里给按钮选"调用函数 → `embark_ui_open_clock`" |

### 10.2 软约束

| # | 不要做 | 会怎样 |
| --- | --- | --- |
| 7 | **把 `lv_obj_t*` 存成成员变量** | 切走再回来，指针指向已销毁对象。**每次都要 `aidogui_ui_bridge_widget(屏名, 控件名)` 现取** |
| 8 | **每帧无条件重写控件文字** | 每次 `lv_label_set_text` 都会分配内存，一秒几百次 → 掉帧、碎片。**变了才写**（§4 的 `last_cs_`） |
| 9 | **`name()` 与屏名不一致** | 不报错，只是点进去黑屏 + 一条 WARN。这是新手 bug 的第一名 |

> 顺带说一条不属于 App 的红线但你会看到：`core/include/embark/hal/*.h` 里**不许出现芯片型号名**
> （`AGENTS.md`）。那是开发基建的人要守的，你写 App 不用管。

---

## 11. 卡住了怎么办 · 需要时读

### 11.1 症状 → 原因 → 动作

| 症状 | 最可能的原因 | 怎么办 |
| --- | --- | --- |
| 点磁贴进去**黑屏**，日志有 `没有屏 xxx` 的 WARN | `name()` 和屏名不一致，或改完设计器忘了导出 | `export.ps1` 再跑一次；核对 `name()` 返回的字符串 |
| 编译：`undefined reference to 'XxxApp::onCreate(...)'` | 头文件里声明了，`.cpp` 里没实现 | 补实现；或者忘了把新 `.cpp` 加进 `EMBARK_APP_SOURCES` |
| 编译：`'xxx' marked 'override', but does not override` | 签名对不上（`const` 漏了、参数类型不同、`noexcept` 不匹配） | 跟 `core/include/embark/app.h` 里的声明逐字对照 |
| 编译：`use of deleted function` / `cannot allocate` | 你碰了被禁的东西（拷 `App`、`std::string`、异常） | 换成 `etl::array` / `string_view`，别抛异常 |
| 跑起来**磁贴上没有我的 App** | 没进 `EMBARK_APP_TABLE`（表在 `host/ui_demo.cpp`） | 把 `XxxApp` 加到表里（**整个程序只能有一张表**） |
| `--screen stopwatch` 报"没有名叫 stopwatch 的 App" | 同上，App 没进表 | 同上。报错里会列出所有可用的名字，很有用 |
| **按钮点了没反应** | ① 事件只在 `onCreate` 挂过（第二次进来失灵）② 改文字改错了控件 ③ 忘了 `ui_backend_tick()` | 事件挂到 `ui_backend_bind_repaint` 的回调里；文字改 `Xxx_L`；`tick` 放函数第一句 |
| 界面**一片空白**但没 WARN | 控件名取不到（`aidogui_ui_bridge_widget` 返回 `nullptr`，你的代码静默跳过） | 用 `if (auto* obj = ...; obj == nullptr) ELOG_WARN("没有控件 {}", "L_Time");` 定位 |
| 日志里**没有**我打的那条 | 整行超 384 字节被丢了（§8.3），或级别被过滤 | 缩短内容；换成多条打印 |
| 读数不涨 / 状态不更新 | 没在 `onForegroundTick` 里推值，或"变了才写"的缓存没重置 | 检查 `last_cs_` 之类缓存的初始化（用 `0xFFFFFFFF` 这种不可能的值） |
| 切走再回来**读数归零** | `onPause` 里把"起点时间"清掉了 | 只清"本次开始"，保留"已累计" |
| 切走再回来**崩溃 / 花屏** | 存了 `lv_obj_t*` 野指针 | 改成每次现取（§10 软约束 7） |
| 改了 `settings()` 但后台还是不跑 | `period_ms = 0`；或 App 从没进过前台（`arm = on_first_enter`）；或后台槽用满（8 个） | 看 boot 日志的"后台配置"那段（§7.1） |
| 界面卡顿 | 钩子里干了重活 / 每帧重写控件 / 在 tick 里打日志 | 按 §3.8 三条硬边界逐条排 |

### 11.2 三个"立刻能确认"的手段

1. **看 boot 日志**：`App xxx 后台配置` 一行不多不少，每个 App 都有。
   没有这行 = 你的 App 没进表。
2. **拿截图**（宿主，无头也能截）：
   ```bash
   ./build/host/embark_host_ui.exe --frames 60 --screenshot probe.bmp --delay 40
   ```
   出来的 `probe.bmp` 打开看 —— 界面到底长什么样，比猜快。
3. **合成点击**（宿主）：`--click` 是内置的两步故事线 —— 在 launcher 屏点时钟卡切到 clock，
   再在 clock 屏点你给的坐标（`--click 120,220`）。它只对 launcher/clock 有意义；
   想给自己的 App 做自动点击，要在 `host/ui_demo.cpp` 里加桩（进阶，见那段文件头的说明）。
   注意：**只要用了合成输入就必须 `--delay 40`**，否则 LVGL 还没到读输入的时刻，点击不会生效。

### 11.3 还是不行

- 把 `ELOG_INFO` 加粗一圈：每个钩子一行、每个分支一行。日志噪音不可怕，
  **"不知道走到哪"**才可怕。
- 读一遍 `docs/reference/pitfalls.md`（实测踩过的坑清单）和
  `docs/concepts/app-lifecycle.md`（生命周期与后台的完整说明）。
- 真机（烧 ESP32）不在这篇范围里：那是 `device/README.md` 和
  `device/tools/idf.ps1` 的世界，宿主上先跑通再上板。

---

## 附：全篇速查

| 我要… | 去哪 |
| --- | --- |
| 装环境、第一次跑起来 | `docs/guides/getting-started.md` |
| 新建一个 App | §2（`node tools/new_app.mjs <名字> --title <中文> --apply`） |
| 画界面 | §4.1 + `docs/guides/html-to-screen.md` |
| 记不清 13 个钩子哪个该放什么 | §3.1 总表 + §3.11 自查表 |
| 想抄一个完整 App | §4（秒表全文） |
| 要不要后台 | §5.2 三个问题 |
| 状态机 / 消息 | §6 |
| 格式化 / 日志 / etl | §7 / §8 / §9 |
| 别干什么 | §10 红线表 |
| 报错 / 黑屏 / 点了没反应 | §11 |
