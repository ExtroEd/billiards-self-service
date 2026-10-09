#ifndef WEB_SERVER_APP_H
#define WEB_SERVER_APP_H

#include "esp_err.h"

/**
 * @brief Запуск HTTP веб-сервера и DNS Captive Portal
 */
void web_server_app_start(void);

/**
 * @brief Остановка HTTP веб-сервера и DNS Captive Portal
 */
void web_server_app_stop(void);

#endif // WEB_SERVER_APP_H
