// Credential-driven Algorand demo: params → balance → signed payment submit.

#include "algorand.h"

#include "algorand_codec.h"

#include <cstring>

#include "esp_log.h"

#if __has_include("algorand_credentials.h")
#include "algorand_credentials.h"
#else
#error "Copy include/algorand_credentials.example.h to include/algorand_credentials.h"
#endif

namespace {
constexpr const char *TAG = "algorand";
// Rounds the txn stays valid after algod's last-round.
constexpr uint64_t kTxnValidityWindow = 1000;

// Static buffers to keep large decode/sign buffers off the task stack.
uint8_t s_private_key[64];
uint8_t s_sender[32];
uint8_t s_receiver[32];
uint8_t s_genesis_hash[32];
uint8_t s_signed_txn[512];

uint64_t maxUint64(uint64_t a, uint64_t b) { return a > b ? a : b; }

// Algorand account secrets store the pubkey in bytes 32–63; must match address.
esp_err_t verifyCredentials(void) {
  uint8_t private_key[64] = {};
  uint8_t address_pubkey[32] = {};

  esp_err_t err = algorandDecodePrivateKeyB64(ALGORAND_PRIVATE_KEY_B64,
                                              private_key);
  if (err != ESP_OK) {
    return err;
  }

  err = algorandDecodeAddress(ALGORAND_ADDRESS, address_pubkey);
  if (err != ESP_OK) {
    return err;
  }

  if (std::memcmp(private_key + 32, address_pubkey, 32) != 0) {
    ESP_LOGE(TAG, "Private key does not match ALGORAND_ADDRESS");
    return ESP_FAIL;
  }

  return ESP_OK;
}
}  // namespace

esp_err_t algorandFetchAndLogTransactionParams(AlgodTxnParams *params_out) {
  if (params_out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  AlgodTxnParams params = {};
  const esp_err_t err =
      algodGetTransactionParams(ALGOD_URL, ALGOD_TOKEN, &params);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to fetch transaction params from %s", ALGOD_URL);
    return err;
  }

  ESP_LOGI(TAG, "Network: %s", ALGORAND_NETWORK);
  ESP_LOGI(TAG, "Consensus version: %s", params.consensus_version);
  ESP_LOGI(TAG, "Genesis ID: %s", params.genesis_id);
  ESP_LOGI(TAG, "Genesis hash: %s", params.genesis_hash_b64);
  ESP_LOGI(TAG, "Last round: %llu",
           static_cast<unsigned long long>(params.last_round));
  ESP_LOGI(TAG, "Suggested fee: %llu microAlgos",
           static_cast<unsigned long long>(params.fee));
  ESP_LOGI(TAG, "Min fee: %llu microAlgos",
           static_cast<unsigned long long>(params.min_fee));

  *params_out = params;
  return ESP_OK;
}

esp_err_t algorandFetchAndLogAccountBalance(void) {
  uint64_t amount = 0;
  const esp_err_t err = algodGetAccountAmount(ALGOD_URL, ALGOD_TOKEN,
                                              ALGORAND_ADDRESS, &amount);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to fetch account balance for %s", ALGORAND_ADDRESS);
    return err;
  }

  ESP_LOGI(TAG, "Account: %s", ALGORAND_ADDRESS);
  ESP_LOGI(TAG, "Balance: %llu microAlgos",
           static_cast<unsigned long long>(amount));
  ESP_LOGI(TAG, "Balance: %llu.%06llu Algos",
           static_cast<unsigned long long>(amount / 1000000),
           static_cast<unsigned long long>(amount % 1000000));

  return ESP_OK;
}

esp_err_t algorandSendConfiguredPayment(const AlgodTxnParams *params,
                                        char *tx_id_out, size_t tx_id_len) {
  if (params == nullptr || tx_id_out == nullptr || tx_id_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = verifyCredentials();
  if (err != ESP_OK) {
    return err;
  }

  uint64_t balance = 0;
  err = algodGetAccountAmount(ALGOD_URL, ALGOD_TOKEN, ALGORAND_ADDRESS, &balance);
  if (err != ESP_OK) {
    return err;
  }

  const uint64_t fee = maxUint64(params->fee, params->min_fee);
  const uint64_t total_cost =
      fee + static_cast<uint64_t>(PAYMENT_AMOUNT_MICROALGOS);
  if (balance < total_cost) {
    ESP_LOGE(TAG, "Insufficient balance: have %llu, need %llu microAlgos",
             static_cast<unsigned long long>(balance),
             static_cast<unsigned long long>(total_cost));
    return ESP_FAIL;
  }

  size_t genesis_hash_len = 0;

  err = algorandDecodePrivateKeyB64(ALGORAND_PRIVATE_KEY_B64, s_private_key);
  if (err != ESP_OK) {
    return err;
  }
  err = algorandDecodeAddress(ALGORAND_ADDRESS, s_sender);
  if (err != ESP_OK) {
    return err;
  }
  err = algorandDecodeAddress(PAYMENT_RECEIVER, s_receiver);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Invalid PAYMENT_RECEIVER address");
    return err;
  }
  err = algorandDecodeBase64(params->genesis_hash_b64, s_genesis_hash,
                             sizeof(s_genesis_hash), &genesis_hash_len);
  if (err != ESP_OK || genesis_hash_len != 32) {
    ESP_LOGE(TAG, "Invalid genesis hash from algod params");
    return ESP_FAIL;
  }

  size_t signed_txn_len = 0;
  err = algorandEncodeSignedPayment(
      s_sender, s_receiver, PAYMENT_AMOUNT_MICROALGOS, fee, params->last_round,
      params->last_round + kTxnValidityWindow, s_genesis_hash, params->genesis_id,
      s_private_key, s_signed_txn, sizeof(s_signed_txn), &signed_txn_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to sign payment transaction");
    return err;
  }

  ESP_LOGI(TAG, "Submitting payment: %llu microAlgos to %s (fee %llu)",
           static_cast<unsigned long long>(PAYMENT_AMOUNT_MICROALGOS),
           PAYMENT_RECEIVER, static_cast<unsigned long long>(fee));

  err = algodSubmitRawTransaction(ALGOD_URL, ALGOD_TOKEN, s_signed_txn,
                                  signed_txn_len, tx_id_out, tx_id_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to submit transaction");
    return err;
  }

  ESP_LOGI(TAG, "Transaction ID: %s", tx_id_out);
  return ESP_OK;
}
