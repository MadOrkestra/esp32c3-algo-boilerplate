# esp32c3-algo-boilerplate

ESP32-C3 Super Mini boilerplate using PlatformIO and ESP-IDF. Connects to WiFi with auto-reconnect, status LED on GPIO 8, USB Serial/JTAG logging, and a minimal SDK-free Algorand client (algod HTTP, msgpack payment encoding, Ed25519 signing).

After WiFi connects, a dedicated task talks to algod in three stages:

1. Fetch and log suggested transaction parameters
2. Fetch and log the configured account balance
3. Sign and submit a configured payment, then log the transaction ID

## Prerequisites

- [PlatformIO](https://platformio.org/)
- Cursor/VS Code with extensions: **PlatformIO IDE**, **clangd**
- Python 3 (optional, for host helper scripts)

## Hardware

This project targets the **ESP32-C3 Super Mini** (USB-C, onboard LED on GPIO 8, WiFi). PlatformIO uses the `esp32-c3-devkitm-1` board profile, which matches the Super Mini pinout.

| Store | Link |
|-------|------|
| AliExpress | [ESP32-C3 Super Mini](https://s.click.aliexpress.com/e/_c3iekZAd) |
| Amazon US | [ESP32-C3 Super Mini](https://amzn.to/49V8xgT) |
| Amazon Germany | [ESP32-C3 Super Mini](https://amzn.to/4sv2isB) |

You need a USB data cable for flashing and serial monitoring over the built-in USB Serial/JTAG interface.

## Setup

### 1. WiFi credentials

```bash
cp include/wifi_credentials.example.h include/wifi_credentials.h
```

Edit `include/wifi_credentials.h` with your SSID and password.

### 2. Algorand credentials

```bash
cp include/algorand_credentials.example.h include/algorand_credentials.h
```

Edit `include/algorand_credentials.h`:

| Field | Description |
|-------|-------------|
| `ALGORAND_NETWORK` | Label for logs (`testnet`, `localnet`, etc.) |
| `ALGOD_URL` | Algod base URL (must be reachable from the ESP32, not `localhost`) |
| `ALGOD_TOKEN` | Algod API token (`""` if none) |
| `ALGORAND_ADDRESS` | Sender address (58-character base32) |
| `ALGORAND_PRIVATE_KEY_B64` | 64-byte account secret as 128-char hex or base64 |
| `PAYMENT_RECEIVER` | Payment recipient address |
| `PAYMENT_AMOUNT_MICROALGOS` | Payment amount in microAlgos |

`algorand_credentials.h` is gitignored and will not be committed.

#### Deriving credentials from a 24-word BIP39 wallet

Algokit and other modern wallets derive accounts with Peikert HD paths (`m/44'/283'/account'/0/index`). The firmware needs the **64-byte account secret** (32-byte signing scalar + 32-byte public key), not the raw BIP39 seed.

Install host dependencies once:

```bash
pip install mnemonic xhd-wallet-api py-algorand-sdk
```

Run the derivation script (paste your mnemonic when prompted, or pass it as an argument):

```bash
python scripts/derive_algorand_credentials.py
python scripts/derive_algorand_credentials.py "word1 word2 ... word24"
python scripts/derive_algorand_credentials.py --account 0 --index 0
```

Copy the printed `ALGORAND_ADDRESS` and `ALGORAND_PRIVATE_KEY_B64` values into `include/algorand_credentials.h`.

For classic single-account keys (25-word Algorand mnemonic or `goal account export -b`), use the exported base64 or hex value directly.

> **Note:** BIP39 HD accounts use BIP32-Ed25519 (Peikert) signing. The derived scalar+pubkey format passes address verification on-device, but transaction signing currently uses standard Ed25519. Use a `goal`-exported key for end-to-end payment submission until Peikert signing is added to the firmware.

#### Local Algokit sandbox

If algod runs in Docker on your development machine, use your machine's LAN IP (for example `http://192.168.1.42:4001`), not `localhost`. The ESP32 cannot reach `localhost` on your computer.

### 3. Build, upload, and monitor

```bash
pio run
pio run -t upload && pio device monitor
```

First build generates `sdkconfig.esp32-c3-supermini` and `compile_commands.json` locally.

## Expected serial output

On success you should see log lines similar to:

```
I (…) algorand: Network: testnet
I (…) algorand: Last round: …
I (…) algorand: Balance: … Algos
I (…) algorand: Submitting payment: … microAlgos to …
I (…) algorand: Transaction ID: …
```

On failure the `algorand` task retries every 30 seconds. Common errors:

| Log message | Likely cause |
|-------------|--------------|
| `Private key does not match ALGORAND_ADDRESS` | Raw BIP39 seed in `ALGORAND_PRIVATE_KEY_B64` instead of derived account secret |
| `Failed to fetch transaction params` | Wrong `ALGOD_URL`, token, or network unreachable from ESP32 |
| `Insufficient balance` | Sender account needs enough microAlgos for amount + fee |

## Host scripts

| Script | Purpose |
|--------|---------|
| `scripts/derive_algorand_credentials.py` | BIP39 mnemonic → address + private key for `algorand_credentials.h` |
| `scripts/verify_payment_encoding.py` | Golden-vector helper for payment txn msgpack (requires `py-algorand-sdk`) |

## IDE (clangd)

IntelliSense is provided by **clangd** via `compile_commands.json`. Microsoft C/C++ IntelliSense is disabled in `.vscode/settings.json`.

### Fix "Conflicted extensions" warning

clangd conflicts with the **C/C++** extension (`ms-vscode.cpptools`), which PlatformIO installs as a dependency. Disabling IntelliSense in settings is not enough — you must disable the extension for this workspace:

1. Open **Extensions** (Cmd+Shift+X)
2. Search **C/C++** (publisher: Microsoft)
3. Click the gear icon → **Disable (Workspace)**

Reload the window. Only **clangd** and **PlatformIO IDE** should provide language features here.

After changing sdkconfig or adding components:

```bash
pio run -t compiledb
```

Then run **clangd: Restart language server** in the editor.

## Project layout

```
platformio.ini              PlatformIO environment
sdkconfig.defaults          HTTPS cert bundle, custom partition table, size opts
partitions.csv              Flash partition table
src/
  main.cpp                  WiFi + algorand task orchestration
  wifi_manager.cpp          WiFi connect / reconnect / LED
  algod_client.cpp          esp_http_client GET/POST to algod
  algorand.cpp              Stage 1–3 APIs and credential checks
  algorand_codec.cpp        Base32/base64, msgpack pay txn, Ed25519 sign
  ed25519/                  Vendored Ed25519 implementation
include/
  wifi_credentials.example.h
  algorand_credentials.example.h
scripts/                    Host helpers (derivation, encoding verification)
open-api-specs/             Algod OpenAPI spec (reference)
```

## LED status (GPIO 8, active LOW)

| State        | Pattern                          |
|--------------|----------------------------------|
| Connected    | Solid on                         |
| Connecting   | 400 ms blink                     |
| Disconnected | 3 blinks, pause, repeat          |
