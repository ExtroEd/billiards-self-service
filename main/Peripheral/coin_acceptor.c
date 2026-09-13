#include "coin_acceptor.h"
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/task.h"

static QueueHandle_t g_coin_queue = NULL;

static volatile int pulse_count = 0;
static volatile int64_t last_pulse_time = 0;
static volatile int64_t last_isr_time = 0;
static volatile bool new_pulse_received = false;

static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

// Обработчик прерывания (ISR)
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int64_t current_time = esp_timer_get_time();
    
    if ((current_time - last_isr_time) > DEBOUNCE_TIME_US) {
        pulse_count++;
        last_pulse_time = current_time;
        new_pulse_received = true;
        last_isr_time = current_time;
    }
}

// Задача проверки таймаута пачки импульсов
static void coin_acceptor_task(void *pvParameters) {
    while (1) {
        int64_t now = esp_timer_get_time();

        if (new_pulse_received && (now - last_pulse_time > COIN_TIMEOUT_US)) {
            taskENTER_CRITICAL(&spinlock);
            int total_pulses = pulse_count;
            pulse_count = 0;
            new_pulse_received = false;
            taskEXIT_CRITICAL(&spinlock);

            // Отправляем количество импульсов в очередь для обработки системой
            if (g_coin_queue != NULL && total_pulses > 0) {
                xQueueSend(g_coin_queue, &total_pulses, portMAX_DELAY);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void coin_acceptor_init(QueueHandle_t coin_queue) {
    g_coin_queue = coin_queue;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << COIN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(COIN_PIN, gpio_isr_handler, NULL);

    // Запуск фоновой задачи обработки таймаутов
    xTaskCreate(coin_acceptor_task, "coin_acceptor_task", 2048, NULL, 10, NULL);
}
