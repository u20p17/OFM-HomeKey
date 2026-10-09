#pragma once
#include <cstddef>
#include <cstdint>
using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_FAIL = 2, NVS_READWRITE = 0;
int nvs_open_from_partition(const char*, const char*, int, nvs_handle_t*);
int nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
int nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
int nvs_commit(nvs_handle_t);
int nvs_erase_all(nvs_handle_t);
