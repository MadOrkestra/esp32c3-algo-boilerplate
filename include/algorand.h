#pragma once

// High-level Algorand demo stages. Reads network/account config from
// algorand_credentials.h and logs results over serial.

#include "algod_client.h"
#include "esp_err.h"

// Stage 1: fetch suggested txn params from algod and log them.
esp_err_t algorandFetchAndLogTransactionParams(AlgodTxnParams *params_out);

// Stage 2: fetch and log the configured sender account balance.
esp_err_t algorandFetchAndLogAccountBalance(void);

// Stage 3: sign PAYMENT_RECEIVER / PAYMENT_AMOUNT_MICROALGOS and submit.
// tx_id_out receives the algod response txId string.
esp_err_t algorandSendConfiguredPayment(const AlgodTxnParams *params,
                                        char *tx_id_out, size_t tx_id_len);
