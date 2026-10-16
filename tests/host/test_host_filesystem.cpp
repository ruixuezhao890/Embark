// HAL 文件系统 · 宿主后端（issue 17 验收项：打开 / 缺失文件 / EOF 语义 / 关闭幂等）。
//
// 单独一个测试目标而不是塞进 embark_tests：这里链的是 embark::platform_host，跑的是真
// stdio 后端（不是 tests/fakes 的替身）；embark_tests 故意不链平台库，见 tests/CMakeLists.txt
// 头注释里的重复定义说明。
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "host_filesystem.h"

namespace {

using embark::Error;
using embark::hal::FileHandle;
using embark::platform::host::HostFileSystem;

/// 每个用例一块干净的根目录（相对当前工作目录：ctest 把工作目录设在构建目录，可写）。
/// 故意不用 temp_directory_path()：本机 TEMP 是 8.3 短名（C:\Users\RUIXUE~1.LAP\...），
/// 在它下面 create_directories 会返回 EACCES（Permission denied，实测），而根目录
/// 本来就是 EMBARK_HOST_FS_ROOT 给的，测试不需要系统临时目录。
std::string fresh_root(const char* name) {
  const std::filesystem::path dir = std::filesystem::current_path() / "embark_host_fs_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir.string();
}

void set_root_env(const std::string& root) {
#if defined(_WIN32)
  _putenv_s("EMBARK_HOST_FS_ROOT", root.c_str());
#else
  setenv("EMBARK_HOST_FS_ROOT", root.c_str(), 1);
#endif
}

void write_fixture(const std::string& root, const char* name, const char* bytes) {
  std::ofstream out(std::filesystem::path(root) / name, std::ios::binary);
  out.write(bytes, static_cast<std::streamsize>(std::strlen(bytes)));
}

std::string read_text(HostFileSystem& fs, FileHandle file, std::size_t bytes) {
  std::string out(bytes, '\0');
  const auto got = fs.read(
      file, etl::span<std::uint8_t>(reinterpret_cast<std::uint8_t*>(out.data()), out.size()));
  if (!got.has_value()) {
    return "<error>";
  }
  out.resize(*got);
  return out;
}

}  // namespace

TEST_CASE("文件系统：没 init 就用 → not_ready") {
  set_root_env(fresh_root("embark_host_fs_uninit"));
  HostFileSystem fs;  // 故意不 init
  CHECK(fs.open("hello.bin").error() == Error::not_ready);
  CHECK(fs.root()[0] == '\0');
}

TEST_CASE("文件系统：非法路径 → invalid_argument") {
  set_root_env(fresh_root("embark_host_fs_paths"));
  HostFileSystem fs;
  REQUIRE(fs.init() == Error::none);
  CHECK(fs.open("").error() == Error::invalid_argument);
  CHECK(fs.open("/etc/passwd").error() == Error::invalid_argument);         // 绝对路径
  CHECK(fs.open("../secret").error() == Error::invalid_argument);           // 跳出根目录
  CHECK(fs.open("a/../../b").error() == Error::invalid_argument);           // 中途跳出
  CHECK(fs.open("C:/Windows/win.ini").error() == Error::invalid_argument);  // 盘符由后端定
  CHECK(fs.open("./hello.bin").error() == Error::invalid_argument);  // '.' 开头（含 "./"）
}

TEST_CASE("文件系统：打开、分段读、EOF 是 0 不是错误、关闭幂等") {
  const std::string root = fresh_root("embark_host_fs_read");
  write_fixture(root, "hello.bin", "hello world");  // 11 字节
  set_root_env(root);
  HostFileSystem fs;
  REQUIRE(fs.init() == Error::none);

  const auto opened = fs.open("hello.bin");
  REQUIRE(opened.has_value());
  const FileHandle file = *opened;
  CHECK(file != embark::hal::invalid_file_handle);

  const auto bytes = fs.size(file);
  REQUIRE(bytes.has_value());
  CHECK(*bytes == 11);

  std::uint8_t first[8] = {};
  const auto n1 = fs.read(file, etl::span<std::uint8_t>(first, sizeof(first)));
  REQUIRE(n1.has_value());
  CHECK(*n1 == 8);
  CHECK(std::memcmp(first, "hello wo", 8) == 0);

  std::uint8_t rest[8] = {};
  const auto n2 = fs.read(file, etl::span<std::uint8_t>(rest, sizeof(rest)));
  REQUIRE(n2.has_value());
  CHECK(*n2 == 3);  // 短读：只剩 3 字节，不是错误
  CHECK(std::memcmp(rest, "rld", 3) == 0);

  std::uint8_t over[4] = {};
  const auto n3 = fs.read(file, etl::span<std::uint8_t>(over, sizeof(over)));
  REQUIRE(n3.has_value());
  CHECK(*n3 == 0);  // 文件尾：0，不是 io_failure

  CHECK(fs.close(file) == Error::none);
  CHECK(fs.close(file) == Error::none);  // 幂等
  CHECK(fs.read(file, etl::span<std::uint8_t>(over, sizeof(over))).error() == Error::not_found);
  CHECK(fs.size(file).error() == Error::not_found);
}

TEST_CASE("文件系统：seek 中段 / 文件尾 / 越界，句柄合法性") {
  const std::string root = fresh_root("embark_host_fs_seek");
  write_fixture(root, "digits.bin", "0123456789");  // 10 字节
  set_root_env(root);
  HostFileSystem fs;
  REQUIRE(fs.init() == Error::none);
  const auto opened = fs.open("digits.bin");
  REQUIRE(opened.has_value());
  const FileHandle file = *opened;

  CHECK(read_text(fs, file, 10) == "0123456789");
  CHECK(fs.seek(file, 4) == Error::none);
  CHECK(read_text(fs, file, 6) == "456789");
  CHECK(fs.seek(file, 10) == Error::none);  // == size：合法位置（EOF）
  CHECK(read_text(fs, file, 1).empty());
  CHECK(fs.seek(file, 11) == Error::invalid_argument);  // 越过文件尾

  std::uint8_t byte = 0;
  CHECK(fs.seek(embark::hal::invalid_file_handle, 0) == Error::invalid_argument);
  CHECK(fs.close(embark::hal::invalid_file_handle) == Error::invalid_argument);
  CHECK(fs.read(static_cast<FileHandle>(99), etl::span<std::uint8_t>(&byte, 1)).error() ==
        Error::invalid_argument);
  CHECK(fs.size(static_cast<FileHandle>(99)).error() == Error::invalid_argument);
  CHECK(fs.close(file) == Error::none);
}

TEST_CASE("文件系统：缺失文件 not_found，子目录路径可用") {
  const std::string root = fresh_root("embark_host_fs_lookup");
  set_root_env(root);
  HostFileSystem fs;
  REQUIRE(fs.init() == Error::none);
  CHECK(fs.open("nope.bin").error() == Error::not_found);

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(root) / "sub", ec);
  write_fixture(std::filesystem::path(root).string(), "sub/inner.bin", "xyz");
  const auto opened = fs.open("sub/inner.bin");
  REQUIRE(opened.has_value());
  CHECK(read_text(fs, *opened, 3) == "xyz");
  CHECK(fs.close(*opened) == Error::none);
}

TEST_CASE("文件系统：槽位用完 → no_space，关掉一个后可再开") {
  const std::string root = fresh_root("embark_host_fs_slots");
  write_fixture(root, "a.bin", "a");
  set_root_env(root);
  HostFileSystem fs;
  REQUIRE(fs.init() == Error::none);

  FileHandle handles[embark::filesystem_max_open_files] = {};
  for (std::size_t i = 0; i < embark::filesystem_max_open_files; ++i) {
    const auto opened = fs.open("a.bin");
    REQUIRE(opened.has_value());
    handles[i] = *opened;
  }
  CHECK(fs.open("a.bin").error() == Error::no_space);  // 调用方没关文件

  CHECK(fs.close(handles[0]) == Error::none);
  CHECK(fs.open("a.bin").has_value());  // 腾出槽位即可再开
  for (std::size_t i = 1; i < embark::filesystem_max_open_files; ++i) {
    CHECK(fs.close(handles[i]) == Error::none);
  }
}
