#pragma once

// Copy this file to algorand_credentials.h and fill in your network details.
// algorand_credentials.h is gitignored and will not be committed.

#define ALGORAND_NETWORK "testnet"
#define ALGOD_URL "https://testnet-api.algonode.cloud"
#define ALGOD_TOKEN ""

#define ALGORAND_ADDRESS "YOUR58CHARALGORANDADDRESSHERE0000000000000000000000"
// 64-byte account secret (32-byte scalar + 32-byte pubkey): 128-char hex or
// base64 from `goal account export -b`. For a 24-word BIP39 wallet, run:
//   pip install mnemonic xhd-wallet-api py-algorand-sdk
//   python scripts/derive_algorand_credentials.py
// Do not paste the raw BIP39 seed (64 bytes) here.
#define ALGORAND_PRIVATE_KEY_B64 "your-base64-or-hex-private-key"

#define PAYMENT_RECEIVER "YOUR58CHARALGORANDADDRESSHERE0000000000000000000000"
#define PAYMENT_AMOUNT_MICROALGOS 0
