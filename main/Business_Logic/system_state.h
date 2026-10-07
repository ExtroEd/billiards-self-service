#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

// Опережающее объявление типа дескриптора I2C устройства без лишних инклюдов
typedef struct i2c_master_dev_t *i2c_master_dev_handle_t;

// Силовой пин управления реле стола
#define RELAY_GPIO_PIN GPIO_NUM_17

/**
 * @brief Инициализация состояния системы, NVS, GPIO реле и мьютексов.
 * 
 * @param ds3231_dev Дескриптор инициализированного I2C устройства DS3231 RTC
 */
void system_state_init(i2c_master_dev_handle_t ds3231_dev);

/**
 * @brief Добавить сумму в сомах (при оплате через QR/Finik или тестировании)
 * @param amount Внесенная сумма в сомах
 */
void system_state_add_credit(int amount);

/**
 * @brief Получить текущий неиспользованный баланс в сомах
 */
int system_state_get_balance(void);

/**
 * @brief Получить оставшееся время сессии в секундах
 */
int system_state_get_remaining_seconds(void);

/**
 * @brief Принудительно сбросить баланс, очистить сессию в NVS и выключить реле
 */
void system_state_reset_balance(void);

/**
 * @brief Установить минимальный порог старта (в сомах)
 */
void system_state_set_min_threshold(int min_soms);

/**
 * @brief Получить текущий минимальный порог старта
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
 * @brief Получить значение общей накопленной кассы
 */
int32_t system_state_get_total_money(void);

/**
 * @brief Сбросить общую кассу в 0 (обнуление инкассации)
 */
void system_state_reset_total_money(void);

/**
 * @brief Получить текущий аппаратный статус реле (true = включено)
 */
bool system_state_is_relay_active(void);

/**
 * @brief Инициализация SNTP для фоновой синхронизации точного времени по Wi-Fi
 */
void system_state_init_sntp(void);

/**
 * @brief Потокобезопасный прием входящих MQTT-команд от Cloudflare Worker
 */
void system_state_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len);

#endif // SYSTEM_STATE_H
