#ifndef DISPLAY_7SEG_H
#define DISPLAY_7SEG_H

#include <stdint.h>
#include "driver/gpio.h"

#define TM1637_CLK_PIN GPIO_NUM_22
#define TM1637_DIO_PIN GPIO_NUM_21

/**
 * @brief Инициализация GPIO пинов TM1637
 */
void display_7seg_init(void);

/**
 * @brief Отобразить время в формате ММ:СС или число минут
 * @param minutes Минуты
 * @param seconds Секунды
 * @param show_colon Показывать ли двоеточие между цифрами
 */
void display_7seg_show_time(int minutes, int seconds, bool show_colon);

/**
 * @brief Отобразить просто целое число (например, оставшиеся минуты)
 * @param value Число для отображения (0..9999)
 */
void display_7seg_show_number(int value);

/**
 * @brief Очистить индикатор
 */
void display_7seg_clear(void);

#endif // DISPLAY_7SEG_H
