#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define COIN_PIN           GPIO_NUM_18
#define COIN_TIMEOUT_US    (250 * 1000) // 250 мс пачка
#define DEBOUNCE_TIME_US   (60 * 1000)
#define SOM_PER_PULSE      1

static volatile int pulse_count = 0;
static volatile int64_t last_pulse_time = 0;
static volatile int64_t last_isr_time = 0;
static volatile bool new_pulse_received = false;

// Обработчик прерывания (ISR) с защитой от дребезга
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int64_t current_time = esp_timer_get_time();
    
    if ((current_time - last_isr_time) > DEBOUNCE_TIME_US) {
        pulse_count++;
        last_pulse_time = current_time;
        new_pulse_received = true;
        last_isr_time = current_time;
    }
}

void app_main(void) {
    // Настройка GPIO 18
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << COIN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     // ОБЯЗАТЕЛЬНО: Внутренний Pull-Up от наводок
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE        // Прерывание по спаду (FALLING)
    };
    gpio_config(&io_conf);

    // Установка службы прерываний GPIO
    gpio_install_isr_service(0);
    gpio_isr_handler_add(COIN_PIN, gpio_isr_handler, NULL);

    printf("=========================================\n");
    printf("ESP-IDF: Защита от помех включена (GPIO 18)\n");
    printf("=========================================\n");

    while (1) {
        int64_t now = esp_timer_get_time();

        if (new_pulse_received && (now - last_pulse_time > COIN_TIMEOUT_US)) {
            portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
            taskENTER_CRITICAL(&myMutex);
            int total_pulses = pulse_count;
            pulse_count = 0;
            new_pulse_received = false;
            taskEXIT_CRITICAL(&myMutex);

            int inserted_soms = total_pulses * SOM_PER_PULSE;
            printf("Импульсов: %d | Принято: %d сом\n", total_pulses, inserted_soms);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
