#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "system_state.h"
#include "Peripheral/encoder.h"
#include "Peripheral/display_tft.h"
#include "Peripheral/ds3231.h"
#include "Networking_Services/wifi_app.h"

#define I2C_SDA_PIN GPIO_NUM_33
#define I2C_SCL_PIN GPIO_NUM_32

static const char *TAG = "MAIN";

static QueueHandle_t encoder_events_queue = NULL;

void app_main(void) {
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "ESP-IDF: Старт системы");
    ESP_LOGI(TAG, "=========================================");

    // 1. Создаем очередь для энкодера
    encoder_events_queue = xQueueCreate(10, sizeof(encoder_event_t));

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("MAIN", "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
    }

    // 2. Инициализация DS3231 RTC
    i2c_master_dev_handle_t ds3231_dev = NULL;
    if (ds3231_init(I2C_SDA_PIN, I2C_SCL_PIN, &ds3231_dev) == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 RTC успешно инициализирован.");
    } else {
        ESP_LOGE(TAG, "Ошибка инициализации DS3231 RTC!");
    }

    // 3. Инициализируем систему и передаем хэндл RTC
    system_state_init(ds3231_dev);
    encoder_init(encoder_events_queue);
    display_tft_init();

    // 4. Включение сетевого стека и Wi-Fi
    wifi_app_init();

    if (wifi_app_get_mode() == WIFI_APP_MODE_AP) {
        ESP_LOGW(TAG, "Система работает в режиме конфигурации SoftAP (192.168.4.1)");
    }

    ESP_LOGI(TAG, "Система готова к работе.");

    encoder_event_t enc_event;

    while (1) {
        display_tft_tick();

        if (xQueueReceive(encoder_events_queue, &enc_event, 0) == pdTRUE) {
            menu_process_event(enc_event);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
