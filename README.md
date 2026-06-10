# esp32c3-algo-boilerplate

ESP32-C3 Super Mini boilerplate using PlatformIO and ESP-IDF. Connects to WiFi with auto-reconnect, status LED on GPIO 8, and USB Serial/JTAG logging.

## Prerequisites

- [PlatformIO](https://platformio.org/)
- Cursor/VS Code with extensions: **PlatformIO IDE**, **clangd**

## Setup

1. Copy WiFi credentials:

   ```bash
   cp include/wifi_credentials.example.h include/wifi_credentials.h
   ```

   Edit `include/wifi_credentials.h` with your SSID and password.

2. Build:

   ```bash
   pio run
   ```

   First build generates `sdkconfig.esp32-c3-supermini` and `compile_commands.json` locally.

3. Upload and monitor:

   ```bash
   pio run -t upload && pio device monitor
   ```

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
platformio.ini          PlatformIO environment
sdkconfig.defaults      Committed Kconfig overrides
src/                    Application source
include/                Headers and wifi_credentials.example.h
```

## LED status (GPIO 8, active LOW)

| State        | Pattern                          |
|--------------|----------------------------------|
| Connected    | Solid on                         |
| Connecting   | 400 ms blink                     |
| Disconnected | 3 blinks, pause, repeat          |
