# 写你的第一个 App（给只会 Arduino 的人）

> **这节解决什么问题**：你在 launcher 里能点开 clock 了，现在想自己做一个 App。
> 这篇从「一条命令建 App」讲到「后台策略、状态机、消息怎么用」，全程宿主模拟器，不碰真机。

---

## 0. 这篇给谁 · 必读 · 5 分钟

你会玩 Arduino：写过 `setup()` / `loop()`、`digitalWrite()`、`Serial.println()`。
你没写过 C++ 的类，没用过 STL，看到 `template<typename...>` 就想关掉。这篇是给你写的。

**读完你会**：一条命令建出 App；知道 13 个可覆写点各自该写什么、绝对不能写什么；
用状态机和消息把逻辑理顺；判断自己的 App 要不要后台 —— 以及为什么**通常答案是"不要"**。
**不会讲的**：烧真机、改 HAL、动 `platform/`（那是另一条线，见 `platform/esp32/README.md`）。
全程宿主模拟器：一个窗口，改代码 → 编译 → 看窗口。

**先决条件**：你已按 [getting-started.md](getting-started.md) 跑起 launcher、能点进 clock。
这篇从「launcher 里多出一块写着"秒表"的磁贴」开始。

| 词 | 是什么 | 在哪 |
| --- | --- | --- |
| **App** | 一个类（`class`），一个 `.h` + 一个 `.cpp` | `app/<名字>/` |
| **屏** | 设计器里画的一张界面 | `app/aidogui_ui/screen.aid`；**屏名必须等于 App 名** |
| **钩子** | 框架在特定时刻调你的成员函数 | 你的 `.cpp` 里 |

> **一句话世界观**：Arduino 把所有事塞进一个 `loop()`；Embark 把 `loop()` 拆成
> 「什么时候该干这件事」的几个钩子 —— 前台每帧一次的是 `onForegroundTick`，
> 第一次进前台的是 `onEnter`，收到消息的是 `onMessage`。
> **你不再自己写 `loop()`，而是往对应的钩子里放对应的活。** 这是全篇最重要的一句话。

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
前几个编不过或引入你没准备好的坑；后两个是本仓硬规矩（§7 红线表）。

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
| `delay(1000)` | **不存在** → 记时间戳、下次比差值 | 这里是 ui 循环，阻塞 = 整机卡（§3.5） |
| `Serial.println()` | `ELOG_INFO("...", 参数)` | 看控制台输出 |
| 一个 `.ino` | 一个 `app/<名>/` 目录 | 还要在 3 个地方登记（§2，脚本代劳） |

### 1.6 三种必遇的编译报错

| 报错里出现 | 真实原因 | 干什么 |
| --- | --- | --- |
| `no member named 'xxx' in 'App'` | 钩子名拼错了 | 回来抄 §3 表格里的名字，逐字对 |
| `undefined reference to StopwatchApp::onEnter()` | `.h` 声明了，`.cpp` 没写实现 | 到 `.cpp` 补上函数体 |
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
| `cmake/embark_sources.cmake` 加 2 行 | 让编译器知道有这个文件 |
| `platform/host/ui_demo.cpp` 加 2 行（include + App 表里加一项） | **App 表 = 注册表**：写进这张表的顺序就是默认前台顺序 |
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
./build/platform/host/embark_host_ui.exe --delay 40
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

App 基类在 `include/embark/app.h`。它有 **13 个你能覆写的东西：6 个必须写，7 个有默认实现**。
"可覆写点"就是框架给你留的插槽，它的集合是冻结的（`app.h:14-15` 明写「除此之外不再增加」）——
所以你只要把这 13 个认全，就不会遇到"还有什么我没实现"的意外。

### 3.1 总表

| # | 名字 | 必须写? | 何时被调 | 一句话职责 |
| --- | --- | --- | --- | --- |
| 1 | `name()` | ✅ 纯虚 | 任何时候（框架随时问） | 身份，且**必须等于屏名** |
| 2 | `onCreate(Framework&)` | ✅ 纯虚 | 装配期，**恰好 1 次** | 存 `fw_`、取 HAL 能力 |
| 3 | `onEnter()` | ✅ 纯虚 | **≤1 次**：第一次成为前台 | 挂屏、启动状态机 |
| 4 | `onPause()` | ✅ 纯虚 | **每次**离开前台 | 存档、停自己的节拍 |
| 5 | `onResume()` | ✅ 纯虚 | **每次**回到前台 | **重新挂屏**、恢复显示 |
| 6 | `onExit()` | ✅ 纯虚 | **恰好 1 次**，且只在关机 | 收尾、落盘 |
| 7 | `title()` | ➖ 有默认 | 画磁贴时 | 给人看的名字（默认 = `name()`） |
| 8 | `icon()` | ➖ 有默认 | 画磁贴时 | `LV_SYMBOL_*`（默认 `nullptr`） |
| 9 | `accent()` | ➖ 有默认 | 需要强调色时 | 主题色（不许写死颜色值） |
| 10 | `settings()` | ➖ 有默认 | boot 时读一次 | 后台策略（§5 专讲） |
| 11 | `onForegroundTick(now_ms)` | ➖ 有默认 | 在前台时**每帧一次** | 把数据推给控件（§3.5） |
| 12 | `onMessage(const Message&)` | ➖ 有默认 | 每收到一条广播 | 按消息 id 分发（§6） |
| 13 | `onBackgroundTick(now_ms)` | ➖ 有默认 | 后台武装后按周期 | 离开前台也要推进的活（§5） |

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
  建**静态**资源；登记界面回调（`ui_backend_bind_click` / `ui_backend_bind_repaint`）。
- **别放**：`publish()` —— 总线这时**还没装配**，发出去没人收，只会打一条 WARN + 计一次 unknown
  （`src/embark/framework.cpp:48-54`，注释原文「装配消息请放到 onEnter 之后」）。
  别碰控件（屏还没加载，取控件返回 `nullptr`）。别做耗时活（这是 boot 期，卡住就是开机慢）。
- **判据**："只做一次、跟你在不在前台无关" → 放这里。

#### ② `onEnter()` —— 第一次上场

- **该放**：挂自己的屏 `ui_backend_load_for_app(name());`；启动状态机 `fsm_.start();`；
  初始化"从头开始"的状态（秒表归零、列表滚到顶）。
- **别放**："每次回来都要做的事" —— `onEnter` **只调一次**（见 3.2 推论 1）。
  想省事就写一个私有 `enter_screen()`，`onEnter` 和 `onResume` 都调它 —— 这是全仓通行写法。
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

### 3.4 七个有默认的钩子：什么时候才需要覆写

| 钩子 | 默认行为 | 你什么时候要覆写它 |
| --- | --- | --- |
| `title()` | 返回 `name()` | 几乎总是 —— 磁贴上要显示中文 |
| `icon()` | 返回 `nullptr`（用标题首字符兜底） | 想要图标时返回 `LV_SYMBOL_*`（如 `LV_SYMBOL_PLAY`） |
| `accent()` | 主题强调色 | 想要专属色调时，**从 design tokens 取**，不许写 `0xFF0000` 这种魔法数 |
| `settings()` | 全部默认（= 不跑后台） | 只有确实需要后台时（§5）；**默认就是正确答案** |
| `onForegroundTick(now_ms)` | 空实现 | 要往界面上刷数据时（几乎总是，见 §3.5） |
| `onMessage(msg)` | 空实现 | 要接收广播时（§6） |
| `onBackgroundTick(now_ms)` | 空实现，而且**默认一次都不会被调** | 声明了后台策略之后才有意义（§5） |

### 3.5 `onForegroundTick(now_ms)`：你的"loop()"，以及三条硬边界

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
   这里是 ui 循环，5 ms 一跳，你卡 100 ms 就是全局掉 20 帧。
2. **不许乱要内存**：没有 `new`/`malloc`，不在栈上放大数组（ui 任务栈 8 KB）。
3. **不许假设被调的频率**：来了就干、干完就返回。要"每 500 ms 一次"就自己比时间戳
   （`if (now_ms - last_ >= 500) { last_ = now_ms; ... }`），**不要**用计数器猜帧率 ——
   帧率会随负载变。

> `now_ms` 是框架取好的单调递增毫秒，直接用。要主动问时间才用 `fw_->hal().time.now_ms()`
> （`hal().time` 是取墙钟的唯一入口）。
