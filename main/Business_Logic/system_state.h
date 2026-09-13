#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>

#define SOM_PER_PULSE 1

/**
 * @brief Инициализация состояния системы (мьютексы/переменные)
 */
void system_state_init(void);

/**
 * @brief Добавить баланс (на основе полученных импульсов)
 * @param pulses Количество полученных импульсов
 */
void system_state_add_pulses(int pulses);

/**
 * @brief Получить текущий баланс сомов
 * @return int Текущий баланс
 */
int system_state_get_balance(void);

#endif // SYSTEM_STATE_H
