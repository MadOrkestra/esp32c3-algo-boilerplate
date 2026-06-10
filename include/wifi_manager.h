#pragma once

// Non-blocking WiFi station manager with auto-reconnect and status LED patterns.

bool wifiManagerBegin();
void wifiManagerLoop();
bool wifiManagerIsConnected();
