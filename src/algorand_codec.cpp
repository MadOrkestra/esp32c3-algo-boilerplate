// Canonical msgpack codec and Ed25519 signing for Algorand pay transactions.

#include "algorand_codec.h"

extern "C" {
#include "ed25519.h"
}

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "mbedtls/base64.h"

namespace {
constexpr const char *TAG = "algo_codec";
constexpr char kTxDomain[] = "TX";
constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

// Keep large crypto buffers off the task stack (Ed25519 is stack-heavy).
uint8_t s_txn_bytes[256];
uint8_t s_sign_message[258];
uint8_t s_signature[64];
uint8_t s_scratch[72];

int base32Value(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= '2' && c <= '7') {
    return c - '2' + 26;
  }
  return -1;
}

// Msgpack unsigned integer encoding (matches algosdk canonical form).
size_t writeUint64(uint8_t *out, uint64_t value) {
  if (value <= 0x7f) {
    out[0] = static_cast<uint8_t>(value);
    return 1;
  }
  if (value <= 0xff) {
    out[0] = 0xcc;
    out[1] = static_cast<uint8_t>(value);
    return 2;
  }
  if (value <= 0xffff) {
    out[0] = 0xcd;
    out[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[2] = static_cast<uint8_t>(value & 0xff);
    return 3;
  }
  if (value <= 0xffffffffULL) {
    out[0] = 0xce;
    out[1] = static_cast<uint8_t>((value >> 24) & 0xff);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[3] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[4] = static_cast<uint8_t>(value & 0xff);
    return 5;
  }

  out[0] = 0xcf;
  for (int i = 0; i < 8; ++i) {
    out[1 + i] = static_cast<uint8_t>((value >> (56 - 8 * i)) & 0xff);
  }
  return 9;
}

size_t writeString(uint8_t *out, const char *value) {
  const size_t len = std::strlen(value);
  size_t pos = 0;

  if (len <= 31) {
    out[pos++] = static_cast<uint8_t>(0xa0 | len);
  } else if (len <= 255) {
    out[pos++] = 0xd9;
    out[pos++] = static_cast<uint8_t>(len);
  } else {
    return 0;
  }

  std::memcpy(out + pos, value, len);
  return pos + len;
}

size_t writeBinary(uint8_t *out, const uint8_t *data, size_t len) {
  if (len <= 255) {
    out[0] = 0xc4;
    out[1] = static_cast<uint8_t>(len);
    std::memcpy(out + 2, data, len);
    return 2 + len;
  }
  return 0;
}

size_t writeMapHeader(uint8_t *out, size_t pair_count) {
  if (pair_count <= 15) {
    out[0] = static_cast<uint8_t>(0x80 | pair_count);
    return 1;
  }
  return 0;
}

size_t encodePaymentTxn(uint8_t *out, size_t out_cap, const uint8_t sender[32],
                        const uint8_t receiver[32], uint64_t amount,
                        uint64_t fee, uint64_t first_valid,
                        uint64_t last_valid, const uint8_t genesis_hash[32],
                        const char *genesis_id) {
  uint8_t scratch[72];
  size_t pos = 0;

  const auto append = [&](const uint8_t *data, size_t len) -> bool {
    if (pos + len > out_cap) {
      return false;
    }
    std::memcpy(out + pos, data, len);
    pos += len;
    return true;
  };

  const auto appendKey = [&](const char *key) -> bool {
    const size_t n = writeString(scratch, key);
    return n > 0 && append(scratch, n);
  };

  const auto appendUint = [&](uint64_t value) -> bool {
    const size_t n = writeUint64(scratch, value);
    return n > 0 && append(scratch, n);
  };

  // Omit amt when zero — required for canonical encoding accepted by algod.
  const size_t field_count = amount > 0 ? 9 : 8;
  const size_t header_len = writeMapHeader(scratch, field_count);
  if (header_len == 0 || !append(scratch, header_len)) {
    return 0;
  }

  if (amount > 0) {
    if (!appendKey("amt") || !appendUint(amount)) {
      return 0;
    }
  }
  if (!appendKey("fee") || !appendUint(fee)) {
    return 0;
  }
  if (!appendKey("fv") || !appendUint(first_valid)) {
    return 0;
  }
  if (!appendKey("gen")) {
    return 0;
  }
  {
    const size_t n = writeString(scratch, genesis_id);
    if (n == 0 || !append(scratch, n)) {
      return 0;
    }
  }
  if (!appendKey("gh")) {
    return 0;
  }
  {
    const size_t n = writeBinary(scratch, genesis_hash, 32);
    if (n == 0 || !append(scratch, n)) {
      return 0;
    }
  }
  if (!appendKey("lv") || !appendUint(last_valid)) {
    return 0;
  }
  if (!appendKey("rcv")) {
    return 0;
  }
  {
    const size_t n = writeBinary(scratch, receiver, 32);
    if (n == 0 || !append(scratch, n)) {
      return 0;
    }
  }
  if (!appendKey("snd")) {
    return 0;
  }
  {
    const size_t n = writeBinary(scratch, sender, 32);
    if (n == 0 || !append(scratch, n)) {
      return 0;
    }
  }
  if (!appendKey("type")) {
    return 0;
  }
  {
    const size_t n = writeString(scratch, "pay");
    if (n == 0 || !append(scratch, n)) {
      return 0;
    }
  }

  return pos;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool isHexString(const char *input) {
  if (input == nullptr) {
    return false;
  }
  const size_t len = std::strlen(input);
  if (len == 0 || (len % 2) != 0) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (hexValue(input[i]) < 0) {
      return false;
    }
  }
  return true;
}

esp_err_t decodeHex(const char *input, uint8_t *out, size_t out_cap,
                    size_t *out_len) {
  const size_t in_len = std::strlen(input);
  if (in_len % 2 != 0 || in_len / 2 > out_cap) {
    return ESP_ERR_INVALID_SIZE;
  }

  for (size_t i = 0; i < in_len; i += 2) {
    const int hi = hexValue(input[i]);
    const int lo = hexValue(input[i + 1]);
    if (hi < 0 || lo < 0) {
      return ESP_FAIL;
    }
    out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
  }

  *out_len = in_len / 2;
  return ESP_OK;
}
}  // namespace

esp_err_t algorandDecodeBase64(const char *input, uint8_t *out, size_t out_cap,
                               size_t *out_len) {
  if (input == nullptr || out == nullptr || out_len == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t written = 0;
  const int rc = mbedtls_base64_decode(out, out_cap, &written,
                                       reinterpret_cast<const unsigned char *>(
                                           input),
                                       std::strlen(input));
  if (rc != 0) {
    ESP_LOGE(TAG, "Base64 decode failed: %d (input len %u, out cap %u)", rc,
             static_cast<unsigned>(std::strlen(input)),
             static_cast<unsigned>(out_cap));
    return ESP_FAIL;
  }

  *out_len = written;
  return ESP_OK;
}

// Base32-decode address; first 32 bytes are the public key (checksum not verified).
esp_err_t algorandDecodeAddress(const char *address, uint8_t out[32]) {
  if (address == nullptr || out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t decoded[36] = {};
  size_t in_len = std::strlen(address);
  while (in_len > 0 && address[in_len - 1] == '=') {
    --in_len;
  }

  size_t out_len = 0;
  uint32_t buffer = 0;
  int bits = 0;

  for (size_t i = 0; i < in_len; ++i) {
    const int value = base32Value(address[i]);
    if (value < 0) {
      ESP_LOGE(TAG, "Invalid base32 character in address");
      return ESP_FAIL;
    }

    buffer = (buffer << 5) | static_cast<uint32_t>(value);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      if (out_len >= sizeof(decoded)) {
        return ESP_ERR_NO_MEM;
      }
      decoded[out_len++] = static_cast<uint8_t>((buffer >> bits) & 0xff);
    }
  }

  if (out_len < 32) {
    ESP_LOGE(TAG, "Unexpected decoded address length: %u", static_cast<unsigned>(out_len));
    return ESP_FAIL;
  }

  std::memcpy(out, decoded, 32);
  return ESP_OK;
}

esp_err_t algorandDecodePrivateKeyB64(const char *private_key_b64,
                                      uint8_t out[64]) {
  if (private_key_b64 == nullptr || out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t len = 0;
  esp_err_t err = ESP_FAIL;

  if (isHexString(private_key_b64)) {
    err = decodeHex(private_key_b64, out, 64, &len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Hex private key decode failed");
      return err;
    }
    ESP_LOGI(TAG, "Using hex-encoded private key");
  } else {
    err = algorandDecodeBase64(private_key_b64, out, 64, &len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG,
               "Private key must be base64 (88 chars) or hex (128 chars); "
               "use: goal account export -b");
      return err;
    }
  }

  if (len != 64) {
    ESP_LOGE(TAG, "Private key must be 64 bytes, got %u",
             static_cast<unsigned>(len));
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t algorandEncodeSignedPayment(
    const uint8_t sender[32], const uint8_t receiver[32], uint64_t amount,
    uint64_t fee, uint64_t first_valid, uint64_t last_valid,
    const uint8_t genesis_hash[32], const char *genesis_id,
    const uint8_t private_key[64], uint8_t *signed_txn, size_t signed_txn_cap,
    size_t *signed_txn_len) {
  if (sender == nullptr || receiver == nullptr || genesis_hash == nullptr ||
      genesis_id == nullptr || private_key == nullptr ||
      signed_txn == nullptr || signed_txn_len == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const size_t txn_len = encodePaymentTxn(
      s_txn_bytes, sizeof(s_txn_bytes), sender, receiver, amount, fee,
      first_valid, last_valid, genesis_hash, genesis_id);
  if (txn_len == 0) {
    ESP_LOGE(TAG, "Failed to encode payment transaction");
    return ESP_FAIL;
  }

  // Algorand signs Ed25519 over "TX" || msgpack(txn).
  std::memcpy(s_sign_message, kTxDomain, sizeof(kTxDomain) - 1);
  std::memcpy(s_sign_message + 2, s_txn_bytes, txn_len);

  ed25519_sign(s_signature, s_sign_message, 2 + txn_len, private_key + 32,
               private_key);

  size_t pos = 0;

  const auto append = [&](const uint8_t *data, size_t len) -> bool {
    if (pos + len > signed_txn_cap) {
      return false;
    }
    std::memcpy(signed_txn + pos, data, len);
    pos += len;
    return true;
  };

  const size_t header_len = writeMapHeader(s_scratch, 2);
  if (header_len == 0 || !append(s_scratch, header_len)) {
    return ESP_ERR_NO_MEM;
  }

  const size_t sig_key_len = writeString(s_scratch, "sig");
  if (sig_key_len == 0 || !append(s_scratch, sig_key_len)) {
    return ESP_ERR_NO_MEM;
  }

  const size_t sig_bin_len =
      writeBinary(s_scratch, s_signature, sizeof(s_signature));
  if (sig_bin_len == 0 || !append(s_scratch, sig_bin_len)) {
    return ESP_ERR_NO_MEM;
  }

  const size_t txn_key_len = writeString(s_scratch, "txn");
  if (txn_key_len == 0 || !append(s_scratch, txn_key_len)) {
    return ESP_ERR_NO_MEM;
  }

  if (!append(s_txn_bytes, txn_len)) {
    return ESP_ERR_NO_MEM;
  }

  *signed_txn_len = pos;
  return ESP_OK;
}
