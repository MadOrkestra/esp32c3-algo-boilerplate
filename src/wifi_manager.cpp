#include "wifi_manager.h"

#include "wifi_config.h"

#include <cstring>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#error "Copy include/wifi_credentials.example.h to include/wifi_credentials.h"
#endif

namespace {
constexpr const char *TAG = "wifi";
constexpr unsigned long RECONNECT_INTERVAL_MS = 10000;
constexpr unsigned long CONNECTING_BLINK_MS = 400;
constexpr unsigned long DISCONNECTED_BLINK_ON_MS = 200;
constexpr unsigned long DISCONNECTED_BLINK_OFF_MS = 200;
constexpr unsigned long DISCONNECTED_PAUSE_MS = 1500;
constexpr uint8_t DISCONNECTED_BLINK_COUNT = 3;
constexpr unsigned long CONNECTING_ATTEMPT_TIMEOUT_MS = 20000;
// 8.5 dBm in ESP-IDF 0.25 dBm units.
constexpr int8_t SUPERMINI_TX_POWER = 34;

enum class WifiLedMode { Connected, Connecting, Disconnected };

unsigned long lastReconnectAttemptMs = 0;
unsigned long lastConnectionAttemptMs = 0;
unsigned long ledPhaseStartMs = 0;
bool eventsRegistered = false;
bool ledInitialized = false;
bool wifiInitialized = false;
bool hasIp = false;
bool lastAttemptFailedHard = false;
WifiLedMode ledMode = WifiLedMode::Connecting;

unsigned long millis() {
  return static_cast<unsigned long>(esp_timer_get_time() / 1000);
}

void setStatusLed(bool on) {
  gpio_set_level(static_cast<gpio_num_t>(WIFI_STATUS_LED_PIN),
                 WIFI_STATUS_LED_ACTIVE_LOW ? !on : on);
}

void initStatusLed() {
  if (ledInitialized) {
    return;
  }

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << WIFI_STATUS_LED_PIN;
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  setStatusLed(false);
  ledInitialized = true;
  ledPhaseStartMs = millis();
}

void setLedMode(WifiLedMode mode) {
  if (ledMode == mode) {
    return;
  }

  ledMode = mode;
  ledPhaseStartMs = millis();
  setStatusLed(mode == WifiLedMode::Connected);
}

void refreshLedMode() {
  if (hasIp) {
    setLedMode(WifiLedMode::Connected);
    return;
  }

  const bool attemptTimedOut =
      millis() - lastConnectionAttemptMs >= CONNECTING_ATTEMPT_TIMEOUT_MS;

  if (lastAttemptFailedHard || attemptTimedOut) {
    setLedMode(WifiLedMode::Disconnected);
    return;
  }

  setLedMode(WifiLedMode::Connecting);
}

void updateConnectingLed(unsigned long now) {
  const unsigned long elapsed = now - ledPhaseStartMs;
  const bool on = (elapsed / CONNECTING_BLINK_MS) % 2 == 0;
  setStatusLed(on);
}

void updateDisconnectedLed(unsigned long now) {
  const unsigned long elapsed = now - ledPhaseStartMs;
  const unsigned long blinkCycleMs =
      DISCONNECTED_BLINK_ON_MS + DISCONNECTED_BLINK_OFF_MS;
  const unsigned long patternMs =
      DISCONNECTED_BLINK_COUNT * blinkCycleMs + DISCONNECTED_PAUSE_MS;
  const unsigned long inPattern = elapsed % patternMs;

  if (inPattern >= DISCONNECTED_BLINK_COUNT * blinkCycleMs) {
    setStatusLed(false);
    return;
  }

  const unsigned long inBlink = inPattern % blinkCycleMs;
  setStatusLed(inBlink < DISCONNECTED_BLINK_ON_MS);
}

void updateStatusLed() {
  initStatusLed();
  refreshLedMode();

  const unsigned long now = millis();
  switch (ledMode) {
  case WifiLedMode::Connected:
    setStatusLed(true);
    break;
  case WifiLedMode::Connecting:
    updateConnectingLed(now);
    break;
  case WifiLedMode::Disconnected:
    updateDisconnectedLed(now);
    break;
  }
}

bool isHardFailureReason(uint8_t reason) {
  return reason == WIFI_REASON_AUTH_FAIL ||
         reason == WIFI_REASON_NO_AP_FOUND ||
         reason == WIFI_REASON_ASSOC_FAIL ||
         reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_CONNECTION_FAIL;
}

void applySuperMiniAntennaFix() {
#if ESP32C3_SUPERMINI_ANTENNA_FIX
  esp_wifi_set_max_tx_power(SUPERMINI_TX_POWER);
  esp_wifi_set_ps(WIFI_PS_NONE);
  ESP_LOGI(TAG, "Super Mini antenna fix applied (TX 8.5 dBm, sleep off)");
#endif
}

void onWifiEvent(void *arg, esp_event_base_t eventBase, int32_t eventId,
                 void *eventData) {
  if (eventBase == WIFI_EVENT) {
    switch (eventId) {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_DISCONNECTED: {
      const auto *disc =
          static_cast<wifi_event_sta_disconnected_t *>(eventData);
      hasIp = false;
      lastAttemptFailedHard = isHardFailureReason(disc->reason);
      lastConnectionAttemptMs = millis();
      setLedMode(WifiLedMode::Connecting);
      ESP_LOGI(TAG, "Disconnected (reason=%d), auto-reconnecting",
               disc->reason);
      esp_wifi_connect();
      break;
    }
    default:
      break;
    }
    return;
  }

  if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
    const auto *event = static_cast<ip_event_got_ip_t *>(eventData);
    hasIp = true;
    lastAttemptFailedHard = false;
    setLedMode(WifiLedMode::Connected);

    wifi_ap_record_t ap = {};
    int8_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      rssi = ap.rssi;
    }

    ESP_LOGI(TAG, "Connected, IP=" IPSTR ", RSSI=%d dBm",
             IP2STR(&event->ip_info.ip), rssi);
  }
}

void registerWifiEvents() {
  if (eventsRegistered) {
    return;
  }

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, nullptr, nullptr));
  eventsRegistered = true;
}

void initWifiStack() {
  if (wifiInitialized) {
    return;
  }

  esp_err_t nvsStatus = nvs_flash_init();
  if (nvsStatus == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvsStatus == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  registerWifiEvents();

  wifi_config_t wifiConfig = {};
  std::strncpy(reinterpret_cast<char *>(wifiConfig.sta.ssid), WIFI_SSID,
               sizeof(wifiConfig.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char *>(wifiConfig.sta.password),
               WIFI_PASSWORD, sizeof(wifiConfig.sta.password) - 1);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
  ESP_ERROR_CHECK(esp_wifi_start());
  applySuperMiniAntennaFix();

  wifiInitialized = true;
}

void beginWifi() {
  initWifiStack();
  lastConnectionAttemptMs = millis();
  lastAttemptFailedHard = false;
  setLedMode(WifiLedMode::Connecting);
  esp_wifi_connect();
}
} // namespace

bool wifiManagerBegin() {
  initStatusLed();
  beginWifi();
  lastReconnectAttemptMs = millis();
  ESP_LOGI(TAG, "Connecting to \"%s\"", WIFI_SSID);
  return hasIp;
}

void wifiManagerLoop() {
  updateStatusLed();

  if (hasIp) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastReconnectAttemptMs < RECONNECT_INTERVAL_MS) {
    return;
  }

  lastReconnectAttemptMs = now;
  ESP_LOGI(TAG, "Still disconnected, retrying connection...");
  beginWifi();
}

bool wifiManagerIsConnected() { return hasIp; }
