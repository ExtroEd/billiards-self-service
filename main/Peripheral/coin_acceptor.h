#ifndef COIN_ACCEPTOR_H
#define COIN_ACCEPTOR_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Конфигурация пинов и интервалов
#define COIN_PIN           GPIO_NUM_18
#define COIN_TIMEOUT_US    (250 * 1000) // 250 мс таймаут завершения пачки
#define DEBOUNCE_TIME_US   (60 * 1000)  // 60 мс антидребезг

/**
 * @brief Инициализация монетоприемника (GPIO, ISR и создание задач)
 * @param coin_queue Очередь FreeRTOS, куда будут отправляться подсчитанные импульсы
 */
void coin_acceptor_init(QueueHandle_t coin_queue);

#endif // COIN_ACCEPTOR_H
