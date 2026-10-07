#ifndef DS3231_H
#define DS3231_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define DS3231_I2C_ADDR 0x68

/**
 * @brief Инициализация шины I2C и регистрация устройства DS3231.
 * 
 * @param sda_pin GPIO для SDA (GPIO 33)
 * @param scl_pin GPIO для SCL (GPIO 32)
 * @param[out] out_dev Указатель на созданный дескриптор устройства
 * @return esp_err_t ESP_OK при успехе
 */
esp_err_t ds3231_init(gpio_num_t sda_pin, gpio_num_t scl_pin, i2c_master_dev_handle_t *out_dev);

/**
 * @brief Получить текущую метку времени UNIX (UTC) с DS3231.
 * 
 * @param dev Дескриптор устройства
 * @param[out] out_time Указатель для сохранения timestamp (секунды)
 * @return esp_err_t ESP_OK при успехе
 */
esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev, int64_t *out_time);

/**
 * @brief Установить время DS3231 из метки времени UNIX (UTC).
 * 
 * @param dev Дескриптор устройства
 * @param unix_time Timestamp в секундах
 * @return esp_err_t ESP_OK при успехе
 */
esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev, int64_t unix_time);

/**
 * @brief Сохранить 32-битное число (оставшиеся секунды) в энергонезависимые регистры DS3231.
 */
esp_err_t ds3231_write_rem_seconds(i2c_master_dev_handle_t dev, int32_t seconds);

/**
 * @brief Прочитать 32-битное число (оставшиеся секунды) из регистров DS3231.
 */
esp_err_t ds3231_read_rem_seconds(i2c_master_dev_handle_t dev, int32_t *out_seconds);

/**
 * @brief Сохранить метку времени последнего выключения (Unix timestamp) в регистры Alarm 2 DS3231.
 */
esp_err_t ds3231_write_last_timestamp(i2c_master_dev_handle_t dev, int64_t timestamp);

/**
 * @brief Прочитать метку времени последнего выключения из регистров Alarm 2 DS3231.
 */
esp_err_t ds3231_read_last_timestamp(i2c_master_dev_handle_t dev, int64_t *out_timestamp);

#endif // DS3231_H
