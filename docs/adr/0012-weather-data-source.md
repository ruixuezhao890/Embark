# 天气数据来源：编译期桩（框架当前没有网络）

用户 2026-10-15 要做「天气」App（launcher 图标入口 + 主屏天气卡入口 + 选城市 + 最近 5 天）。
第一轮拷问里第一个问题就是**数据从哪来**，答案是：**当前没有网络，先做编译期桩**。

事实（不是手册说法，是把仓库点了一遍得到的）：框架的 HAL 面只有 time / persistence / log /
system / bus / display / input —— **没有 WiFi、没有 HTTP client、没有 socket、没有 NTP**。
全仓 `wifi|http|network|sntp|ntp` 只有 5 处命中，且全是注释：
[0004](0004-background-tick-and-state-policy.md) 与 [0005](0005-static-task-slots-and-pool.md)
把「WiFi 非阻塞连接」写成 own_task 策略的**将来**第一个真实用例；
`platform/esp32/project/partitions.csv` 写着 "phy_init is not needed today (no WiFi enabled)"
但已预留；`platform/esp32/README.md` 把 WiFi/BLE 列为未实现。也就是说：**5 天天气没有
任何现成来源**，这是本任务最大的设计缺口，而不是实现细节。

所以本 ADR 决定：

1. **天气是桩，而且明确标成桩**：数据写在 `app/weather/weather_data.h`，是编译期常量表
   （4 城 × 5 天：日期标签 / 天气字 / 一句话 / 温度区间），零堆、零 IO、零错误分支。
   它不是"临时凑的假数据藏在业务里"，而是有名字、有文档、有一处定义的
   （术语见 [GLOSSARY.md](../../GLOSSARY.md) 的 Weather stub）。
2. **不出现在 S 盘**：ADR [0011](0011-aidogui-ui-source.md) 把 S 盘留给**界面资源**（字体
   BIN / 图片 BIN，由设计器导出、按 (字体,字号) 子集化）。天气数据不是界面资源，放 S 盘会
   平白引入文本解析与 IO 错误分支，和 [0003](0003-no-heap-no-exceptions.md) 的零堆/无异常
   精神相冲。
3. **一处数据、两处画**：主屏天气卡（`Card_Weather`）与天气屏读同一张表、同一个持久化键
   （`hal().storage` 的 `wx.city`，一个字节）。卡片不是第二份数据 —— 切完城市回主屏，
   `LauncherApp::refresh_weather()` 会把它刷成同一座城市。
4. **将来接真数据时改这一处**：真数据进来 = 换掉 `weather_data.h` 的实现（或把它降级成
   缓存），`WeatherApp` / `LauncherApp` 的取数接口与界面控件名不变。这条 ADR 到那时被取代。

验收口径（`embark_host_ui --ui-weather`，见 [.scratch/weather/acceptance.md](../../.scratch/weather/acceptance.md)）：
进天气屏 → 天气屏 5 天**逐字段对桩表** → 点「>」换城市 → 回主屏 → 天气卡换成同一座城市；
每一步都顺带做一次**字形覆盖检查**（缺字形只会画成方框，不检查就等于没验 —— 见
[GLOSSARY.md](../../GLOSSARY.md) 的 Glyph coverage guard）。城市名不写死：起始城市来自
持久化，跨次运行会延续（跑两遍 `--ui-weather` 就是持久化的证据）。
`--ui` / `--ui-nav` 必须不回归。
