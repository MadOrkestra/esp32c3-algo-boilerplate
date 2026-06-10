// Algod REST client: JSON GET for params/accounts, binary POST for signed txns.

#include "algod_client.h"

#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "algod";
constexpr size_t kMaxResponseBytes = 4096;
constexpr size_t kMaxUrlBytes = 512;

// Shared response buffer — algod calls are serialized on the algorand task.
char response_buf[kMaxResponseBytes];
size_t response_len = 0;

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
    const int copy_len =
        static_cast<int>(response_len + evt->data_len >= kMaxResponseBytes - 1
                             ? (kMaxResponseBytes - 1) - response_len
                             : evt->data_len);
    if (copy_len > 0) {
      std::memcpy(response_buf + response_len, evt->data, copy_len);
      response_len += static_cast<size_t>(copy_len);
      response_buf[response_len] = '\0';
    }
  }
  return ESP_OK;
}

void copy_string_field(char *dest, size_t dest_size, const cJSON *item,
                       const char *field_name) {
  if (item == nullptr || !cJSON_IsString(item)) {
    ESP_LOGE(TAG, "Missing or invalid field: %s", field_name);
    dest[0] = '\0';
    return;
  }
  std::strncpy(dest, item->valuestring, dest_size - 1);
  dest[dest_size - 1] = '\0';
}

uint64_t get_uint64_field(const cJSON *item, const char *field_name) {
  if (item == nullptr || !cJSON_IsNumber(item)) {
    ESP_LOGE(TAG, "Missing or invalid numeric field: %s", field_name);
    return 0;
  }
  return static_cast<uint64_t>(item->valuedouble);
}

void trim_trailing_slash(char *url) {
  const size_t len = std::strlen(url);
  if (len > 0 && url[len - 1] == '/') {
    url[len - 1] = '\0';
  }
}

esp_err_t algodHttpGet(const char *url, const char *token) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.event_handler = http_event_handler;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return ESP_FAIL;
  }

  if (token != nullptr && token[0] != '\0') {
    esp_http_client_set_header(client, "X-Algo-API-Token", token);
  }

  response_len = 0;
  response_buf[0] = '\0';

  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    return err;
  }
  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status %d: %s", status, response_buf);
    return ESP_FAIL;
  }
  if (response_len == 0) {
    ESP_LOGE(TAG, "Empty response body");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t algodHttpPostBinary(const char *url, const char *token,
                              const uint8_t *body, size_t body_len) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_POST;
  config.event_handler = http_event_handler;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return ESP_FAIL;
  }

  if (token != nullptr && token[0] != '\0') {
    esp_http_client_set_header(client, "X-Algo-API-Token", token);
  }
  esp_http_client_set_header(client, "Content-Type", "application/x-binary");
  esp_http_client_set_post_field(client, reinterpret_cast<const char *>(body),
                                 body_len);

  response_len = 0;
  response_buf[0] = '\0';

  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    return err;
  }
  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status %d: %s", status, response_buf);
    return ESP_FAIL;
  }
  if (response_len == 0) {
    ESP_LOGE(TAG, "Empty response body");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t buildAlgodUrl(char *url, size_t url_size, const char *base_url,
                        const char *path) {
  char base_copy[kMaxUrlBytes];
  std::strncpy(base_copy, base_url, sizeof(base_copy) - 1);
  base_copy[sizeof(base_copy) - 1] = '\0';
  trim_trailing_slash(base_copy);

  const int written = std::snprintf(url, url_size, "%s%s", base_copy, path);
  if (written < 0 || static_cast<size_t>(written) >= url_size) {
    ESP_LOGE(TAG, "URL too long");
    return ESP_ERR_INVALID_SIZE;
  }
  return ESP_OK;
}
}  // namespace

esp_err_t algodGetTransactionParams(const char *base_url, const char *token,
                                    AlgodTxnParams *out) {
  if (base_url == nullptr || out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  char url[kMaxUrlBytes];
  esp_err_t err = buildAlgodUrl(url, sizeof(url), base_url,
                                "/v2/transactions/params");
  if (err != ESP_OK) {
    return err;
  }

  err = algodHttpGet(url, token);
  if (err != ESP_OK) {
    return err;
  }

  cJSON *root = cJSON_Parse(response_buf);
  if (root == nullptr) {
    ESP_LOGE(TAG, "Failed to parse JSON response");
    return ESP_FAIL;
  }

  copy_string_field(out->consensus_version, sizeof(out->consensus_version),
                    cJSON_GetObjectItem(root, "consensus-version"),
                    "consensus-version");
  copy_string_field(out->genesis_id, sizeof(out->genesis_id),
                    cJSON_GetObjectItem(root, "genesis-id"), "genesis-id");
  copy_string_field(out->genesis_hash_b64, sizeof(out->genesis_hash_b64),
                    cJSON_GetObjectItem(root, "genesis-hash"), "genesis-hash");
  out->fee = get_uint64_field(cJSON_GetObjectItem(root, "fee"), "fee");
  out->min_fee =
      get_uint64_field(cJSON_GetObjectItem(root, "min-fee"), "min-fee");
  out->last_round =
      get_uint64_field(cJSON_GetObjectItem(root, "last-round"), "last-round");

  cJSON_Delete(root);

  if (out->genesis_id[0] == '\0' || out->genesis_hash_b64[0] == '\0') {
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t algodGetAccountAmount(const char *base_url, const char *token,
                                const char *address,
                                uint64_t *amount_microalgos) {
  if (base_url == nullptr || address == nullptr ||
      amount_microalgos == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  char url[kMaxUrlBytes];
  char path[kMaxUrlBytes];
  const int path_written = std::snprintf(
      path, sizeof(path), "/v2/accounts/%s?exclude=all", address);
  if (path_written < 0 ||
      static_cast<size_t>(path_written) >= sizeof(path)) {
    ESP_LOGE(TAG, "Account path too long");
    return ESP_ERR_INVALID_SIZE;
  }

  esp_err_t err = buildAlgodUrl(url, sizeof(url), base_url, path);
  if (err != ESP_OK) {
    return err;
  }

  err = algodHttpGet(url, token);
  if (err != ESP_OK) {
    return err;
  }

  cJSON *root = cJSON_Parse(response_buf);
  if (root == nullptr) {
    ESP_LOGE(TAG, "Failed to parse JSON response");
    return ESP_FAIL;
  }

  const uint64_t amount =
      get_uint64_field(cJSON_GetObjectItem(root, "amount"), "amount");
  cJSON_Delete(root);

  *amount_microalgos = amount;
  return ESP_OK;
}

esp_err_t algodSubmitRawTransaction(const char *base_url, const char *token,
                                    const uint8_t *signed_txn,
                                    const size_t signed_txn_len,
                                    char *tx_id_out, const size_t tx_id_len) {
  if (base_url == nullptr || signed_txn == nullptr || tx_id_out == nullptr ||
      tx_id_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  char url[kMaxUrlBytes];
  esp_err_t err =
      buildAlgodUrl(url, sizeof(url), base_url, "/v2/transactions");
  if (err != ESP_OK) {
    return err;
  }

  err = algodHttpPostBinary(url, token, signed_txn, signed_txn_len);
  if (err != ESP_OK) {
    return err;
  }

  cJSON *root = cJSON_Parse(response_buf);
  if (root == nullptr) {
    ESP_LOGE(TAG, "Failed to parse submit response");
    return ESP_FAIL;
  }

  copy_string_field(tx_id_out, tx_id_len, cJSON_GetObjectItem(root, "txId"),
                    "txId");
  cJSON_Delete(root);

  if (tx_id_out[0] == '\0') {
    return ESP_FAIL;
  }

  return ESP_OK;
}
