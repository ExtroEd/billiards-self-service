#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "esp_err.h"
#include <stdbool.h>

// Режимы работы Wi-Fi
typedef enum {
    WIFI_APP_MODE_NONE = 0,
    WIFI_APP_MODE_STA      // Подключен к роутеру
} wifi_app_mode_t;

/**
 * @brief Инициализация NVS и Wi-Fi в режиме клиента (STA).
 */
void wifi_app_init(void);

/**
 * @brief Проверка статуса подключения к роутеру (true - ОК, false - НЕТ)
 */
bool wifi_app_is_connected(void);

/**
 * @brief Проверка текущего режима Wi-Fi
 */
wifi_app_mode_t wifi_app_get_mode(void);

#endif // WIFI_APP_H
