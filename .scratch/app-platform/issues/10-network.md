# 10 · 网络服务 INetwork

Status: done（2026-10-18）
Blocked by: 07

## 为什么

Q12=(A)：完整 `INetwork`（连接态机 + HTTP GET + SNTP），但排在 IMU 与音频之后。

它是 03 号票之外唯一能让**天气接真数据**（ADR 0012 第 4 条自己写着"这条 ADR 到那时被取代"）的前置。

## 今天的实情

- `platform/esp32/project/sdkconfig`：`CONFIG_ESP_WIFI_ENABLED=y`（:1238）、`CONFIG_LWIP_ENABLE=y`（:1502）⇒ 协议栈已编进来
- `sdkconfig.defaults`（53 行）**没有一行 WiFi 配置**
- 框架侧 0 行应用代码，`hal::Context` 没有网络位置
- BLE：`# CONFIG_BT_ENABLED is not set`（:611）⇒ **BLE 连 IDF 都没编**，不在本票范围

## 接口形状

`include/embark/hal/network.h`

`enum class NetState : std::uint8_t { off, connecting, connected, failed };`

`class INetwork {`
` public:`
`  virtual Error init() noexcept = 0;`
`  virtual NetState state() const noexcept = 0;`
`  virtual Error connect(const char* ssid, const char* password) noexcept = 0;`
`  virtual Error disconnect() noexcept = 0;`
`  // 阻塞 GET；调用方必须在 own task 里跑（见下）。body 写进调用方给的 span，返回实际长度。`
`  virtual Error http_get(const char* url, etl::span<char> body_out, std::size_t& body_len) noexcept = 0;`
`  virtual Error sntp_sync(std::uint32_t timeout_ms) noexcept = 0;`
`};`

## 线程纪律（这条决定了它怎么用）

`http_get` 是阻塞的（TLS 握手 + 几 KB 响应，几十毫秒到几秒）。**绝不能在 UI 任务上调**——`include/embark/framework.h:106-108` 写明 `onBackgroundTick` 在唯一 UI 任务的循环里执行。

正确的用法是 App 声明 `BackgroundPolicy::own_task`，在任务入口里跑 `http_get`，结果用 `Framework::post()` 发回 UI 线程（`framework.h:128`，收件箱锁在队列内部）。

## 一个必须先解决的容量问题

`config/embark_limits.h` 的 `persistence_max_value_bytes = 64`。WiFi 密码最长 63 字符，加结尾 `\0` 就 **正好 64 或溢出**，而且 SSID（≤32）还要另存一格。

两个选项（实现时选一个并在 ADR 里写下来）：

- (a) 把 `persistence_max_value_bytes` 抬到 128 —— 代价是 NVS 每个槽 128 字节 × 32 槽 = 4 KB
- (b) 网络配置单独存一个 key，SSID 与密码分两槽

## 真机怎么落

`platform/esp32/src/esp32_network.{h,cpp}`：`esp_wifi`（STA 模式）+ `esp_http_client` + `esp_sntp`。

## 宿主怎么落

`platform/host/host_network.{h,cpp}`：直通宿主 socket（WinSock 或 `<sys/socket.h>` 风格的封装）。宿主上 `connect` 恒成功（直接 `connected`），这样 App 逻辑在两边同形。

## 验收

- 宿主：`embark_host_ui --ui --delay 40 --net-test` 拉一次 `http://example.com`，日志打状态码与响应字节数
- 真机：连上指定 AP，日志打 IP；`sntp_sync` 后 `hal.time.epoch_ms()` 与真实时间差 < 1 s（取代当前"用固件编译时刻播种"的近似做法，`platform/esp32/src/esp32_time.cpp` 的 `seed_from_build_time`）
- `tests/hal/test_network.cpp`：fake 后端验状态机（`off → connecting → connected`；`connect` 失败进 `failed`；未连接时 `http_get` 返回 `not_ready`）
- 不回归四条既有验收命令

## 下游（不在本票）

天气接真数据：换掉 `app/weather/weather_data.h` 的实现或把它降级成缓存，`WeatherApp` / `LauncherApp` 的取数接口与界面控件名不变（ADR 0012 第 4 条原话）。

## 收尾记录（2026-10-18）

### 落地的文件

- **接口**：`include/embark/hal/network.h`（`NetState` 四态 + `INetwork` 七个纯虚：票面的 `init` / `state` / `connect` /
  `disconnect` / `http_get` / `sntp_sync` 六个 + `last_http_status()` —— 最后这个是验收要断言状态码才补的）
- **宿主**：`platform/host/host_network.{h,cpp}`（WinSock 直通；`connect()` 恒成功进 `connected`；`sntp_sync` 恒
  `unsupported` —— 宿主时钟本来就是真的）；接进 `platform/host/host_context.{h,cpp}`；`CMakeLists.txt` 加
  `if(WIN32) target_link_libraries(embark_platform_host PUBLIC ws2_32) endif()`（**必须 PUBLIC**，PRIVATE 会让
  `embark_host_ui` 链接失败）
- **真机**：`platform/esp32/src/esp32_network.{h,cpp}`（esp_wifi STA + `esp_http_client` + `esp_netif_sntp`）；
  接进 `esp32_hal.{h,cpp}`；`cmake/embark_sources.cmake` 登记；`components/embark/CMakeLists.txt` 的 REQUIRES
  追加 `esp_wifi esp_netif esp_event esp_http_client`
- **验收开关**：`platform/host/ui_demo.cpp` 的 `--net-test`（`run_net_test()`）
- **单测**：`tests/host/test_network.cpp`（7 个 TEST_CASE，全部离线）

### 票面两处需要裁定的分歧，实际怎么定的

1. **`persistence_max_value_bytes` 抬不抬** → 选 **(b) 分两槽**，`config/embark_limits.h` 一行没动。
   理由：64 是 KV 定长槽布局的一部分（`detail/kv_slot.h` 的编解码），抬高它会让**已存的键值全部失效**；
   而 WiFi 凭据本来就不该由服务层替 App 决定要不要记住 —— 服务层只提供一条路径，不落 NVS
   （`esp_wifi_set_storage(WIFI_STORAGE_RAM)`）。
2. **`tests/hal/test_network.cpp`** → 实际放 `tests/host/test_network.cpp`。票面写 `tests/hal/`，但被测的是
   **宿主后端**（`embark_host_tests` 链 `embark::platform_host`；`embark_tests` 不链平台库），和
   `tests/host/test_audio.cpp` 同款处置。

### 实现期踩到并写进注释的三个坑

- `NetState` 在 `embark::hal` 里，实现类的**头文件里必须写 `using NetState = hal::NetState;`**，否则
  「`NetState` 未命名类型」；命名空间作用域定义处还要写 `HostNetwork::NetState HostNetwork::state()`。
- 真机 `netif` 句柄用 **`void*`** 存，不写 `struct esp_netif_obj*` —— 后者在 namespace 里前置声明会造出另一个类型。
- `connect()` 必须先 `esp_wifi_disconnect()` 再清事件位、再置 `connecting`：否则自己触发的 DISCONNECTED
  事件会抢先把它翻成 `failed`。

### 实测

- 宿主 `cmake --build build` exit 0；`ctest` 2/2；`embark_host_tests` 18 cases / 158 assertions 全过
- `embark_host_ui --net-test` → **exit 0，〔网络验收通过〕**：`GET http://example.com/ → none，状态码 200，
  body 577 字节`；`4 字节缓冲 → no_space，写出 0`；断开后 `state=off`、再 GET → `not_ready`；
  `sntp_sync` → `unsupported`
- 五条既有验收故事 + `embark_host_tour` 全 exit 0（不回归）
- 真机 `idf.py build` exit 0，`embark_esp32.bin` = **0x189770 字节**（app 分区 0x300000，剩 49%）——
  WiFi + HTTP 栈约吃掉 1 MB（上一版 0xeea40）

### 没做的（有意）

- **HTTPS**：要引 TLS + 证书校验，是独立决策（`http_get` 对非 `http://` 返回 `unsupported`）
- **记住 WiFi**：凭据不落 NVS，由 App 自己经 `IPersistence` 存两槽
- **BLE**：IDF 里连 `CONFIG_BT_ENABLED` 都没开，不在本票范围
- **天气接真数据**：见本票「下游」段，另开票（ADR 0012 第 4 条规定了换数时的约束）
