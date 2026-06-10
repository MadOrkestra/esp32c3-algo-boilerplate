#pragma once

// Algorand encoding helpers: base32 addresses, base64/hex keys, canonical
// msgpack payment transactions, and Ed25519 signing (TX domain prefix).

#include <stddef.h>
#include <stdint.h>

#include "algod_client.h"
#include "esp_err.h"

// Decode a 58-character Algorand address to a 32-byte public key.
esp_err_t algorandDecodeAddress(const char *address, uint8_t out[32]);

// Decode a 64-byte account secret from base64 (~88 chars) or hex (128 chars).
// Layout: bytes 0–31 = signing seed, bytes 32–63 = public key.
esp_err_t algorandDecodePrivateKeyB64(const char *private_key_b64,
                                      uint8_t out[64]);

esp_err_t algorandDecodeBase64(const char *input, uint8_t *out, size_t out_cap,
                               size_t *out_len);

// Build a msgpack SignedTxn { sig, txn } for a canonical pay transaction.
esp_err_t algorandEncodeSignedPayment(
    const uint8_t sender[32], const uint8_t receiver[32], uint64_t amount,
    uint64_t fee, uint64_t first_valid, uint64_t last_valid,
    const uint8_t genesis_hash[32], const char *genesis_id,
    const uint8_t private_key[64], uint8_t *signed_txn, size_t signed_txn_cap,
    size_t *signed_txn_len);
