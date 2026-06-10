#pragma once

// Minimal algod HTTP client (no Algorand SDK). Uses esp_http_client with the
// ESP-IDF certificate bundle for HTTPS endpoints.

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Fields from GET /v2/transactions/params needed to build a payment txn.
typedef struct {
  char consensus_version[64];
  char genesis_id[64];
  char genesis_hash_b64[96];
  uint64_t fee;
  uint64_t min_fee;
  uint64_t last_round;
} AlgodTxnParams;

// GET /v2/transactions/params
esp_err_t algodGetTransactionParams(const char *base_url, const char *token,
                                    AlgodTxnParams *out);

// GET /v2/accounts/{address}?exclude=all — returns only the amount field.
esp_err_t algodGetAccountAmount(const char *base_url, const char *token,
                                const char *address, uint64_t *amount_microalgos);

// POST /v2/transactions — body is a msgpack SignedTxn (application/x-binary).
esp_err_t algodSubmitRawTransaction(const char *base_url, const char *token,
                                    const uint8_t *signed_txn,
                                    size_t signed_txn_len, char *tx_id_out,
                                    size_t tx_id_len);
