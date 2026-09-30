#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>

#define RELAY_GPIO_PIN 17

/**
 * @brief Инициализация состояния системы, NVS, GPIO реле и мьютексов
 */
void system_state_init(void);

/**
 * @brief Добавить сумму напрямую в сомах (при оплате или тестировании)
 * @param amount Внесенная сумма в сомах
 */
void system_state_add_credit(int amount);

/**
 * @brief Получить текущий баланс сомов
 */
int system_state_get_balance(void);

/**
 * @brief Сбросить текущий баланс, обнулить время и выключить реле
 */
void system_state_reset_balance(void);

/**
 * @brief Установить минимальный порог старта (в сомах)
 */
void system_state_set_min_threshold(int min_soms);

/**
 * @brief Получить текущий порог старта
 */
int system_state_get_min_threshold(void);

/**
 * @brief Установить цену за 1 час игры (в сомах)
 */
void system_state_set_price_per_1hour(int price);

/**
 * @brief Получить текущую цену за 1 час
 */
int system_state_get_price_per_1hour(void);

/**
 * @brief Получить значение общей кассы
 */
int32_t system_state_get_total_money(void);

/**
 * @brief Сбросить общую кассу в 0
 */
void system_state_reset_total_money(void);

/**
 * @brief Получить текущий статус реле (включено/выключено)
 */
bool system_state_is_relay_active(void);

#endif // SYSTEM_STATE_H
