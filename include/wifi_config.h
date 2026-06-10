#pragma once

#include <cstdint>

// Early ESP32-C3 Super Mini boards place the crystal too close to the ceramic
// antenna. Lower TX power after WiFi.begin() to reduce reflected RF energy.
#ifndef ESP32C3_SUPERMINI_ANTENNA_FIX
#define ESP32C3_SUPERMINI_ANTENNA_FIX 1
#endif

constexpr bool kEsp32C3SuperMiniAntennaFix = ESP32C3_SUPERMINI_ANTENNA_FIX;

constexpr uint8_t WIFI_STATUS_LED_PIN = 8;
// Super Mini onboard LED on GPIO8 is active LOW.
constexpr bool WIFI_STATUS_LED_ACTIVE_LOW = true;
