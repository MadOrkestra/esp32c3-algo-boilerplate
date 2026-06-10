#include "wifi_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

extern "C" void app_main(void) {
  wifiManagerBegin();
  ESP_LOGI(TAG, "Ready");

  while (true) {
    wifiManagerLoop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
