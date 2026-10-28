#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "system_state.h"
#include "Peripheral/display_tft.h"
#include "Peripheral/ds3231.h"
#include "Networking_Services/wifi_app.h"
#include "UI/ui_menu.h"

#define I2C_SDA_PIN GPIO_NUM_33
#define I2C_SCL_PIN GPIO_NUM_32

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "ESP-IDF: Старт системы");
    ESP_LOGI(TAG, "=========================================");

    // 1. Инициализация DS3231 RTC
    i2c_master_dev_handle_t ds3231_dev = NULL;
    if (ds3231_init(I2C_SDA_PIN, I2C_SCL_PIN, &ds3231_dev) == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 RTC успешно инициализирован.");
    } else {
        ESP_LOGE(TAG, "Ошибка инициализации DS3231 RTC!");
    }

    // 2. Инициализируем систему и передаем хэндл RTC
    system_state_init(ds3231_dev);
    display_tft_init();
    ui_menu_init();

    // 3. Включение сетевого стека и Wi-Fi (только клиентский режим STA)
    wifi_app_init();

    ESP_LOGI(TAG, "Система готова к работе.");

    while (1) {
        // Проверяем условия подсветки дисплея (ошибки статистики или активность реле)
        display_tft_tick();

        // Периодически обновляем экран, чтобы статусы сети/времени актуализировались
        ui_menu_init();

        vTaskDelay(pdMS_TO_TICKS(1000)); // Обновление раз в секунду
    }
}
