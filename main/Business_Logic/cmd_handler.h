#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include <stddef.h>

/**
 * @brief Инициализация очереди и задачи-воркера для обработки MQTT/Telegram команд
 */
void cmd_handler_init(void);

/**
 * @brief Потокобезопасный прием входящих MQTT-команд для обработки
 */
void cmd_handler_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len);

#endif // CMD_HANDLER_H
