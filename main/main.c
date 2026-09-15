#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "coin_acceptor.h"
// #include "system_state.h"
#include "Peripheral/encoder.h"
#include "Peripheral/display_tft.h"

static const char *TAG = "MAIN";

static QueueHandle_t coin_events_queue = NULL;
static QueueHandle_t encoder_events_queue = NULL;

void app_main(void) {
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "ESP-IDF: Старт системы");
    ESP_LOGI(TAG, "=========================================");

    // 1. Создаем очереди
    coin_events_queue = xQueueCreate(10, sizeof(int));
    encoder_events_queue = xQueueCreate(10, sizeof(encoder_event_t));

    // 2. Инициализируем систему и периферию
    system_state_init();
    coin_acceptor_init(coin_events_queue);
    encoder_init(encoder_events_queue);
    display_tft_init(); // Включит подсветку

    ESP_LOGI(TAG, "Готово к тестированию. Покрути или нажми энкодер!");

    int received_pulses = 0;
    encoder_event_t enc_event;

    while (1) {
        // Проверка монетоприемника
        if (xQueueReceive(coin_events_queue, &received_pulses, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Монетоприемник: %d импульсов", received_pulses);
            system_state_add_pulses(received_pulses);
        }

        // Проверка энкодера
        if (xQueueReceive(encoder_events_queue, &enc_event, 0) == pdTRUE) {
            switch (enc_event) {
                case ENCODER_EVENT_UP:
                    ESP_LOGI(TAG, ">>> Энкодер: ВРАЩЕНИЕ ВВЕРХ (По часовой) <<<");
                    break;

                case ENCODER_EVENT_DOWN:
                    ESP_LOGI(TAG, ">>> Энкодер: ВРАЩЕНИЕ ВНИЗ (Против часовой) <<<");
                    break;

                case ENCODER_EVENT_CLICK:
                    ESP_LOGI(TAG, ">>> Энкодер: КЛИК КНОПКИ <<<");
                    break;

                case ENCODER_EVENT_LONG_PRESS:
                    ESP_LOGI(TAG, ">>> Энкодер: ДОЛГОЕ УДЕРЖАНИЕ <<<");
                    break;

                default:
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
