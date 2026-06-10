#!/usr/bin/env python3
"""Golden-vector helper for payment txn msgpack encoding (run on host with py-algorand-sdk)."""

import base64
import json
import sys

from algosdk import encoding, transaction
from algosdk.v2client import algod


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: verify_payment_encoding.py <algod_url> <sender> <receiver> [amount_microalgos]",
            file=sys.stderr,
        )
        return 1

    url = sys.argv[1]
    sender = sys.argv[2]
    receiver = sys.argv[3]
    amount = int(sys.argv[4]) if len(sys.argv) > 4 else 0

    client = algod.AlgodClient("", url)
    params = client.suggested_params()
    sp = transaction.SuggestedParams(
        fee=max(params["fee"], params["min-fee"]),
        first=params["last-round"],
        last=params["last-round"] + 1000,
        gh=params["genesis-hash"],
        gen=params["genesis-id"],
    )
    txn = transaction.PaymentTxn(sender, sp, receiver, amount)
    unsigned_b64 = encoding.msgpack_encode(txn)
    unsigned = base64.b64decode(unsigned_b64)
    print(json.dumps({"unsigned_hex": unsigned.hex(), "unsigned_len": len(unsigned)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
