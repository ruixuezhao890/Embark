/**
 * Embark ESP32-S3 · 持久化后端实现（issues/11）
 */
#include "esp32_persistence.h"

#include <cstring>

#include <embark/error.h>
#include <embark_limits.h>
#include <middleware/elog/elog.hpp>

#include <nvs_flash.h>

namespace embark::platform::esp32 {
namespace {

/// NVS 命名空间（NVS 自己的命名空间上限也是 15 字符）。
constexpr const char* nvs_namespace = "embark";

/// 键名缓冲：NVS_KEY_NAME_MAX_SIZE 是 16（含结尾 '\0'），也就是有效键长 ≤ 15。
constexpr std::size_t key_buffer_bytes = 16U;

/// etl::string_view 不保证 NUL 结尾，NVS 要的是 C 串 —— 先拷进定长缓冲。
/// 返回 false = 键为空、超框架上限、或超 NVS 的 15 字符上限。
bool copy_key(etl::string_view key, char* out) noexcept {
  if (key.empty() || key.size() > embark::persistence_max_key_bytes ||
      key.size() + 1U > key_buffer_bytes) {
    return false;
  }
  std::memset(out, 0, key_buffer_bytes);
  std::memcpy(out, key.data(), key.size());
  return true;
}

/// NVS 的错误码翻译成框架的 Error。
Error from_nvs(esp_err_t error) noexcept {
  switch (error) {
    case ESP_ERR_NVS_NOT_FOUND:
      return Error::not_found;
    case ESP_ERR_NVS_INVALID_LENGTH:
      return Error::no_space;
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
    case ESP_ERR_NVS_NO_FREE_PAGES:
      return Error::no_space;
    case ESP_ERR_NVS_INVALID_NAME:
    case ESP_ERR_NVS_KEY_TOO_LONG:
    case ESP_ERR_NVS_VALUE_TOO_LONG:
      return Error::invalid_argument;
    case ESP_ERR_NVS_INVALID_HANDLE:
    case ESP_ERR_NVS_NOT_INITIALIZED:
      return Error::not_ready;
    case ESP_ERR_NVS_NEW_VERSION_FOUND:
    case ESP_ERR_NVS_INVALID_STATE:
      return Error::corrupt_data;
    default:
      return Error::io_failure;
  }
}

}  // namespace

Esp32Persistence::~Esp32Persistence() {
  if (opened_) {
    nvs_close(handle_);
    opened_ = false;
  }
}

Error Esp32Persistence::init() noexcept {
  if (opened_) {
    return Error::none;  // 幂等
  }

  esp_err_t error = nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // 分区需要重建才可用。擦除是**用户的决定**（会丢掉所有已存设置），这里只如实上报。
    ELOG_WARN("NVS 分区需要重建（{}）：调用 erase_all()，或 idf.py -p <port> erase-flash 后重烧",
              esp_err_to_name(error));
    return Error::corrupt_data;
  }
  if (error != ESP_OK) {
    ELOG_ERROR("nvs_flash_init 失败：{}", esp_err_to_name(error));
    return from_nvs(error);
  }

  error = nvs_open(nvs_namespace, NVS_READWRITE, &handle_);
  if (error != ESP_OK) {
    ELOG_ERROR("nvs_open(\"{}\") 失败：{}", nvs_namespace, esp_err_to_name(error));
    return from_nvs(error);
  }

  opened_ = true;
  return Error::none;
}

etl::expected<std::size_t, Error> Esp32Persistence::read(
    etl::string_view key, etl::span<std::uint8_t> out) const noexcept {
  if (!opened_) {
    return embark::unexpected(Error::not_ready);
  }

  char name[key_buffer_bytes] = {};
  if (!copy_key(key, name)) {
    return embark::unexpected(Error::invalid_argument);
  }

  // 第一步：只问长度（out_value = nullptr 就是"查询"）。
  std::size_t length = 0;
  esp_err_t error = nvs_get_blob(handle_, name, nullptr, &length);
  if (error != ESP_OK) {
    return embark::unexpected(from_nvs(error));
  }
  if (length == 0U) {
    return std::size_t{0};
  }
  // 别人往里塞了超过框架上限的值：那是数据不对，不是"放不下"。
  if (length > embark::persistence_max_value_bytes) {
    return embark::unexpected(Error::corrupt_data);
  }
  if (out.size() < length) {
    return embark::unexpected(Error::no_space);
  }

  // 第二步：真读。
  error = nvs_get_blob(handle_, name, out.data(), &length);
  if (error != ESP_OK) {
    return embark::unexpected(from_nvs(error));
  }
  return length;
}

Error Esp32Persistence::write(etl::string_view key, etl::span<const std::uint8_t> value) noexcept {
  if (!opened_) {
    return Error::not_ready;
  }

  char name[key_buffer_bytes] = {};
  if (!copy_key(key, name)) {
    return Error::invalid_argument;
  }
  if (value.size() > embark::persistence_max_value_bytes) {
    return Error::no_space;  // 值太大：按契约是 no_space，不是 invalid_argument
  }
  if (value.size() > 0U && value.data() == nullptr) {
    return Error::invalid_argument;
  }

  const esp_err_t error = nvs_set_blob(handle_, name, value.data(), value.size());
  if (error != ESP_OK) {
    ELOG_WARN("nvs_set_blob(\"{}\", {} 字节) 失败：{}", name, static_cast<unsigned>(value.size()),
              esp_err_to_name(error));
    return from_nvs(error);
  }

  // 没有 commit 就不算落盘 —— 省略它会让断电后的行为依赖 NVS 的内部时机，太玄。
  const esp_err_t commit = nvs_commit(handle_);
  if (commit != ESP_OK) {
    ELOG_WARN("nvs_commit 失败：{}", esp_err_to_name(commit));
    return from_nvs(commit);
  }
  return Error::none;
}

Error Esp32Persistence::erase(etl::string_view key) noexcept {
  if (!opened_) {
    return Error::not_ready;
  }

  char name[key_buffer_bytes] = {};
  if (!copy_key(key, name)) {
    return Error::invalid_argument;
  }

  const esp_err_t error = nvs_erase_key(handle_, name);
  if (error != ESP_OK) {
    return from_nvs(error);  // 含 ESP_ERR_NVS_NOT_FOUND → not_found（不做静默成功）
  }

  const esp_err_t commit = nvs_commit(handle_);
  return commit == ESP_OK ? Error::none : from_nvs(commit);
}

Error Esp32Persistence::erase_all() noexcept {
  if (!opened_) {
    return Error::not_ready;
  }

  const esp_err_t error = nvs_erase_all(handle_);
  if (error != ESP_OK) {
    return from_nvs(error);
  }

  const esp_err_t commit = nvs_commit(handle_);
  return commit == ESP_OK ? Error::none : from_nvs(commit);
}

std::size_t Esp32Persistence::capacity_bytes() const noexcept {
  return embark::persistence_max_slots * embark::persistence_max_value_bytes;
}

}  // namespace embark::platform::esp32
