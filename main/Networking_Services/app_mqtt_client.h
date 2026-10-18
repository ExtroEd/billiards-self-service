#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"
#include "secrets.h"

// Конфигурация EMQX Cloud
#define MQTT_BROKER_URI     SECRET_MQTT_BROKER_URI
#define MQTT_USER           SECRET_MQTT_USER
#define MQTT_PASS           SECRET_MQTT_PASS

// Динамические макросы топиков на основе TABLE_NUMBER из secrets.h
// Используем функцию макроподстановки через строковые литералы C
#define _STRINGIFY(x) #x
#define _TOSTRING(x) _STRINGIFY(x)

// Или формируем топики динамически прямо при публикации / подписке, 
// но для совместимости зададим базовые через функции или строковые шаблоны.
// Лучше всего формировать топик в самом коде отправки (app_mqtt_client.c), 
// но для старых дефайнов сделаем так:

#define TABLE_ID TABLE_NUMBER

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
 */
esp_err_t mqtt_app_publish_status(const char *payload);

#endif // MQTT_CLIENT_H
