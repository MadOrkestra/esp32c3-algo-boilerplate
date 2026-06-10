// WiFi on the main task; Algorand HTTP/crypto on a dedicated high-stack task.

#include "algorand.h"
#include "wifi_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";
// HTTPS + Ed25519 need far more than the default main-task stack (~4 KB).
static constexpr uint32_t kAlgorandTaskStackBytes = 24576;

static void algorandTask(void * /*param*/) {
  AlgodTxnParams params = {};
  char txId[128] = {};
  unsigned long lastAttemptMs = 0;

  // Wait for DHCP before hitting algod.
  while (!wifiManagerIsConnected()) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  while (true) {
    const unsigned long now =
        static_cast<unsigned long>(esp_timer_get_time() / 1000);
    // Retry the full pipeline every 30 s until all three stages succeed.
    if (lastAttemptMs == 0 || now - lastAttemptMs >= 30000) {
      lastAttemptMs = now;
      if (algorandFetchAndLogTransactionParams(&params) == ESP_OK &&
          algorandFetchAndLogAccountBalance() == ESP_OK &&
          algorandSendConfiguredPayment(&params, txId, sizeof(txId)) ==
              ESP_OK) {
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  vTaskDelete(nullptr);
}

extern "C" void app_main(void) {
  wifiManagerBegin();
  ESP_LOGI(TAG, "Ready");

  xTaskCreate(algorandTask, "algorand", kAlgorandTaskStackBytes, nullptr, 5,
              nullptr);

  while (true) {
    wifiManagerLoop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
