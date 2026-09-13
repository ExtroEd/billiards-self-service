#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "coin_acceptor.h"
#include "system_state.h"

// Очередь для передачи импульсов монетоприемника
static QueueHandle_t coin_events_queue = NULL;

void app_main(void) {
    printf("=========================================\n");
    printf("ESP-IDF: Запуск системы оплаты\n");
    printf("=========================================\n");

    // 1. Создаем очередь сообщений (емкость на 10 событий)
    coin_events_queue = xQueueCreate(10, sizeof(int));

    // 2. Инициализируем состояния системы и периферию
    system_state_init();
    coin_acceptor_init(coin_events_queue);

    int received_pulses = 0;

    while (1) {
        // Ожидаем сигналы от монетоприемника из очереди
        if (xQueueReceive(coin_events_queue, &received_pulses, pdMS_TO_TICKS(100))) {
            // Передаем импульсы в управление состоянием
            system_state_add_pulses(received_pulses);
        }

        // Здесь можно выполнять другие задачи (отрисовка экрана, проверка кнопок и т.д.)
    }
}
