issue 13 证据：上游 efmt 的 1 字节整型成员打印缺陷（复现 + 规避）

结论先说：efmt 的**派生打印路径**（`E_FMT_DERIVE` / `E_FMT_FIELDS`）把 1 字节整型
成员（`std::uint8_t` / `std::int8_t` / `unsigned char`）当**字符**输出，而不是数字；
值为 0 时写出 `\0`，会把整行日志**截断**。**顶层参数不受影响**（`uint8_t` 直接
填空是正常的数字）。本仓库不改 third_party（submodule 钉 commit），只做规避；
是否在 efmt-elog 修由用户决定。

- 环境：GCC 15.1.0（MinGW-w64 / UCRT），efmt-elog 钉 commit
  `cc1bd221c8b3fcf8172ac6cbd2b56bde47d12cf7`，开关全默认
  （`EFMT_DERIVE_SHOW_TYPE 1`、`EFMT_DERIVE_STRICT 1`）。
- 复现文件：本目录不用建工程，直接把下面的探针源码存成 `derive_probe.cpp`，
  按文件头的命令编译即可（不需要 Embark 的 include，只需 efmt）。

--- 探针源码 ---------------------------------------------------------------
```cpp
// 探针：uint8_t 到底被怎么打的
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <middleware/efmt/core/format.hpp>   // 只有 efmt 时改成 <efmt/core/format.hpp>

template <typename T>
void dump(const char* label, const T& value) {
  char buffer[96] = {};
  const std::size_t length = e_fmt::format_to(buffer, sizeof(buffer), "{}", value);
  std::printf("%-14s len=%2u text=|%s| hex=", label, static_cast<unsigned>(length), buffer);
  for (std::size_t i = 0; i < length && i < 64U; ++i) {
    std::printf("%02X ", static_cast<unsigned char>(buffer[i]));
  }
  std::printf("\n");
}

E_FMT_DERIVE(struct Small {
  std::uint8_t a;
  std::uint16_t b;
  std::int8_t c;
  unsigned char d;
  char e;
});

int main() {
  dump("uint8=5", std::uint8_t{5});
  dump("uint8=0", std::uint8_t{0});
  dump("int8=5", std::int8_t{5});
  dump("uint16=300", std::uint16_t{300});
  dump("small", Small{5, 300, 6, 7, 'x'});
  return 0;
}
```

编译：
```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti \
    -include config/embark_config.h -I include -I build/include \
    -I third_party/etl/include derive_probe.cpp -o derive_probe.exe
```

--- 实测输出（GCC 15.1 / MinGW-w64，零警告）--------------------------------
```
uint8=5        len= 1 text=|5| hex=35
uint8=0        len= 1 text=|0| hex=30
int8=5         len= 1 text=|5| hex=35
uint16=300     len= 3 text=|300| hex=33 30 30
small          len=45 text=|Small { a = , b = 300, c = , d = , e = x }| hex=53 6D 61 6C 6C 20 7B 20 61 20 3D 20 05 2C 20 62 20 3D 20 33 30 30 2C 20 63 20 3D 20 06 2C 20 64 20 3D 20 07 2C 20 65 20 3D 20 78 20 7D
```

读法：
- 前四行是**顶层参数**：`uint8_t` / `int8_t` 都走整型通道，输出 `5`、`0`（hex 30）。
- 最后一行是**派生结构体**：`a`（uint8_t=5）的字节是 `05`、`c`（int8_t=6）是 `06`、
  `d`（unsigned char=7）是 `07` —— 都是**原始字节**而不是 `35`/`36`/`37`；
  同一个结构体里的 `b`（uint16_t=300）正确输出 `300`，`e`（char='x'）正确输出 `x`。

影响：1 字节整型**字段值为 0** 时（最常见：`key = 0` 的指针事件、
`key = 0` 的触摸事件、`AppId` 的 0 号 App）会在日志里写出 `\0` ——
下游按 C 字符串处理就会**从那里截断整行**，看起来像"这条日志没打出来"。

--- 上游代码定位（报 issue / 提补丁时用）-----------------------------------
- `third_party/efmt-elog/efmt/core/format_traits.hpp:160-175`：`is_builtin_type`
  的白名单是 `int` / `unsigned` / `long` / `unsigned long` / `long long` /
  `unsigned long long` / `float` / `double` / `bool` / `char` / `const char*` /
  `std::string` / `std::string_view` —— **没有 `short` / `unsigned char` /
  `signed char`**，所以 1 字节整型在派生成员这条路径上落不到整型分支，
  最终按字符处理。
- `format_traits.hpp:133-155`：`default_formatter`（派生 → `E_FMT_FIELDS` →
  否则 `obj@地址`；`EFMT_DERIVE_STRICT=1` 时编译期报错）。
- `format_derive.hpp:1952-1975`：`write_field` / `derive_write_value`；
  `:2385-2429`：`derive_write` 的分支链（char 数组 → 数组 → 指针 →
  `has_derived_formatter_v` → `has_field_names_v` → 带字段聚合体位置式 → 兜底
  `formatter<D>::format(ctx, specs, value)`）。
- 上游手册自身口径不一：`docs/EFMT-使用手册.md:270` 写"小整数类型（`uint8_t`、
  `short`、`char` 除外）都走整型通道"，`:320` 的表格却写 `short`/`int8_t`/
  `uint16_t` 等"统一走整型通道" —— 实测两种说法各对一半（顶层对、派生成员不对）。

--- 本仓库的规避（issue 13）-------------------------------------------------
框架里**会进日志的 1 字节字段一律抬到 `std::uint16_t`**（顶层 2 字节起打印正常）：

| 类型 | 字段 | 原 | 现 |
| --- | --- | --- | --- |
| `embark::AppId`（`include/embark/message.h`） | `AppId` / `CrossTaskMessage::from_app` | `std::uint8_t` | `std::uint16_t`（`invalid_app_id` → `0xFFFFU`） |
| `embark::AppSettings`（`include/embark/app.h`） | `task_priority` | `std::uint8_t` | `std::uint16_t` |
| `embark::hal::InputEvent`（`include/embark/hal/types.h`） | `key` | `std::uint8_t` | `std::uint16_t`（键号约定仍是 0..255） |
| `embark::ITaskSpawner`（`include/embark/task_spawner.h`） | `spawn_task(..., priority)` | `std::uint8_t` | `std::uint16_t`（两个实现同步） |

同时把这条写进 `docs/common-pitfalls.md`（"派生打印"一节）与 `spec.md` §17，
并在 `tests/kernel/test_format_derive.cpp` 里钉住"这些字段打出的是数字"。
