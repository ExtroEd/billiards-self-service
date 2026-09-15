#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

// Перечисление событий энкодера
typedef enum {
    ENCODER_EVENT_NONE = 0,
    ENCODER_EVENT_UP,       // Поворот по часовой
    ENCODER_EVENT_DOWN,     // Поворот против часовой
    ENCODER_EVENT_CLICK,    // Короткое нажатие
    ENCODER_EVENT_LONG_PRESS// Зажатие кнопки
} encoder_event_t;

// Правильное назначение GPIO:
#define ENCODER_CLK_PIN    GPIO_NUM_19
#define ENCODER_DT_PIN     GPIO_NUM_16
#define ENCODER_SW_PIN     GPIO_NUM_25

/**
 * @brief Инициализация GPIO и прерываний для энкодера KY-040
 * @param event_queue Очередь FreeRTOS для отправки событий encoder_event_t
 */
void encoder_init(QueueHandle_t event_queue);

#endif // ENCODER_H
