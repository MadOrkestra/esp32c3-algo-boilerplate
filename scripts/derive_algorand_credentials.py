#!/usr/bin/env python3
"""Derive Algorand account credentials from a 24-word BIP39 mnemonic.

Uses ARC-52 Peikert HD derivation (m/44'/283'/account'/0/index), matching
@algorandfoundation/algokit-utils peikertXHdWalletGenerator + accountGenerator.

Host dependencies:
    pip install mnemonic xhd-wallet-api py-algorand-sdk

Usage:
    python scripts/derive_algorand_credentials.py
    python scripts/derive_algorand_credentials.py "word1 word2 ... word24"
    python scripts/derive_algorand_credentials.py --account 0 --index 0
"""

from __future__ import annotations

import argparse
import base64
import sys

from algosdk import encoding
from mnemonic import Mnemonic
from xhd_wallet_api_py import (
    DerivationScheme,
    KeyContext,
    from_seed,
    key_gen,
    public_key,
)


def read_mnemonic_from_stdin() -> str:
    print("Paste your 24-word BIP39 mnemonic on one line, then press Enter:")
    line = sys.stdin.readline()
    if not line:
        raise SystemExit("No mnemonic provided.")
    return " ".join(line.split())


def derive_account_secret(
    mnemonic: str,
    *,
    account: int,
    index: int,
    passphrase: str,
) -> tuple[str, bytes, bytes]:
    words = mnemonic.split()
    if len(words) not in (12, 15, 18, 21, 24):
        raise ValueError(f"Expected 12/15/18/21/24 words, got {len(words)}")

    if not Mnemonic("english").check(mnemonic):
        raise ValueError("Invalid BIP39 mnemonic checksum.")

    bip39_seed = bytearray(Mnemonic("english").to_seed(mnemonic, passphrase=passphrase))
    root_xprv = from_seed(bip39_seed)
    leaf_xprv = key_gen(
        root_xprv,
        KeyContext.Address,
        account,
        index,
        DerivationScheme.Peikert,
    )
    pubkey = public_key(leaf_xprv)
    # 64-byte Algorand account secret: ed25519 scalar + public key.
    secret_key = bytes(leaf_xprv[:32]) + pubkey
    if len(secret_key) != 64:
        raise RuntimeError(f"Unexpected secret key length: {len(secret_key)}")

    address = encoding.encode_address(pubkey)
    return address, secret_key, bytes(bip39_seed)


def print_credentials(
    address: str,
    secret_key: bytes,
    bip39_seed: bytes,
    *,
    account: int,
    index: int,
) -> None:
    secret_hex = secret_key.hex()
    secret_b64 = base64.b64encode(secret_key).decode("ascii")

    print()
    print(f"# BIP44 path: m/44'/283'/{account}'/0/{index} (Peikert)")
    print(f"# BIP39 seed (for Algokit SENDER_SEED): {bip39_seed.hex()}")
    print()
    print("Copy into include/algorand_credentials.h:")
    print()
    print(f'#define ALGORAND_ADDRESS "{address}"')
    print(f'#define ALGORAND_PRIVATE_KEY_B64 "{secret_hex}"')
    print()
    print("Alternative base64 private key (also accepted by firmware):")
    print(secret_b64)
    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Derive Algorand credentials from a BIP39 mnemonic."
    )
    parser.add_argument(
        "mnemonic",
        nargs="?",
        help="24-word mnemonic (quoted). If omitted, reads one line from stdin.",
    )
    parser.add_argument(
        "--account",
        type=int,
        default=0,
        help="BIP44 account' index (default: 0)",
    )
    parser.add_argument(
        "--index",
        type=int,
        default=0,
        help="BIP44 address index (default: 0)",
    )
    parser.add_argument(
        "--passphrase",
        default="",
        help="Optional BIP39 passphrase",
    )
    args = parser.parse_args()

    mnemonic = args.mnemonic if args.mnemonic else read_mnemonic_from_stdin()

    try:
        address, secret_key, bip39_seed = derive_account_secret(
            mnemonic,
            account=args.account,
            index=args.index,
            passphrase=args.passphrase,
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print_credentials(
        address,
        secret_key,
        bip39_seed,
        account=args.account,
        index=args.index,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
