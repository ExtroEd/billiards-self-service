#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "esp_err.h"
#include <stdbool.h>

// Режимы работы Wi-Fi
typedef enum {
    WIFI_APP_MODE_NONE = 0,
    WIFI_APP_MODE_STA,     // Подключен к роутеру
    WIFI_APP_MODE_AP       // Точка доступа (настройка)
} wifi_app_mode_t;

/**
 * @brief Инициализация NVS и Wi-Fi менеджера.
 * Автоматически пытается подключиться к сохраненному Wi-Fi.
 * Если настроек нет или роутер недоступен — поднимает SoftAP с HTTP-сервером.
 */
void wifi_app_init(void);

// Занудительный запуск SoftAP по кнопке из меню (создает точку доступа)
void wifi_app_start_ap_mode(void);

// Проверка статуса подключения к домашнему роутеру (true - ОК, false - НЕТ)
bool wifi_app_is_connected(void);

/**
 * @brief Проверка текущего режима Wi-Fi
 */
wifi_app_mode_t wifi_app_get_mode(void);

/**
 * @brief Сохранение настроек Wi-Fi в NVS вручную (при необходимости)
 */
esp_err_t wifi_app_save_credentials(const char *ssid, const char *pass);

/**
 * @brief Чтение настроек Wi-Fi из NVS
 */
esp_err_t wifi_app_read_credentials(char *ssid_out, char *pass_out);

// Завершает работу точки доступа (SoftAP) и переподключается к сохранённой сети STA
void wifi_app_stop_ap_and_reconnect(void);

#endif // WIFI_APP_H
