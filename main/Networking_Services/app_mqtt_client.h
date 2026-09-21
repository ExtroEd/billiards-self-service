#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"
#include "secrets.h"

// Конфигурация HiveMQ Cloud
#define MQTT_BROKER_URI     SECRET_MQTT_BROKER_URI
#define MQTT_USER           SECRET_MQTT_USER
#define MQTT_PASS           SECRET_MQTT_PASS

// Топики управления и телеметрии
#define MQTT_TOPIC_CMD      "billiards/table_1/cmd"      // Прием команд (от Cloudflare/админки)
#define MQTT_TOPIC_STATUS   "billiards/table_1/status"   // Отправка статуса и баланса

/**
 * @brief Инициализация и запуск MQTT клиента
 */
esp_err_t mqtt_app_start(void);

/**
 * @brief Проверка текущего статуса подключения к брокеру
 */
bool mqtt_app_is_connected(void);

/**
 * @brief Отправка состояния/телеметрии на брокер
 * 
 * @param payload Текст сообщения (например, JSON или просто число)
 */
esp_err_t mqtt_app_publish_status(const char *payload);

#endif // MQTT_CLIENT_H
