#include "cmd_handler.h"
#include "system_state.h"
#include "Networking_Services/tg_bot.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG_CMD = "CMD_HANDLER";

typedef struct {
    char payload[512];
} mqtt_cmd_msg_t;

static QueueHandle_t s_mqtt_cmd_queue = NULL;

static void mqtt_cmd_worker_task(void *pvParameters) {
    mqtt_cmd_msg_t msg;

    while (1) {
        if (xQueueReceive(s_mqtt_cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG_CMD, "=== [MQTT WORKER] Разбор входящего сообщения ===");

            cJSON *root = cJSON_Parse(msg.payload);
            if (root != NULL) {
                cJSON *event = cJSON_GetObjectItem(root, "event");

                if (event && cJSON_IsString(event)) {
                    const char *evt = event->valuestring;

                    // 1. Пополнение баланса (Оплата)
                    if (strcmp(evt, "PAYMENT_SUCCESS") == 0) {
                        cJSON *amount = cJSON_GetObjectItem(root, "amount");
                        if (amount && cJSON_IsNumber(amount)) {
                            system_state_add_credit(amount->valueint);
                        }
                    }
                    // 2. Запрос статуса / отчёта
                    else if (strcmp(evt, "REQUEST_STATUS") == 0) {
                        cJSON *req_by = cJSON_GetObjectItem(root, "requestedBy");
                        if (req_by && cJSON_IsString(req_by)) {
                            tg_bot_send_status_report(req_by->valuestring);
                        }
                    }
                    // 3. Сброс текущей сессии (Времени и баланса стола)
                    else if (strcmp(evt, "RESET_SESSION") == 0) {
                        system_state_reset_balance();
                        ESP_LOGW(TAG_CMD, "Удаленный сброс сессии выполнен!");

                        cJSON *req_by = cJSON_GetObjectItem(root, "requestedBy");
                        if (req_by && cJSON_IsString(req_by)) {
                            tg_bot_send_text(req_by->valuestring, "✅ <b>Сессия и время успешно сброшены!</b>");
                        }
                    }
                    // 4. Сброс общей кассы (Инкассация)
                    else if (strcmp(evt, "RESET_CASH") == 0) {
                        system_state_reset_total_money();
                        ESP_LOGW(TAG_CMD, "Удаленная инкассация (сброс кассы) выполнена!");

                        cJSON *req_by = cJSON_GetObjectItem(root, "requestedBy");
                        if (req_by && cJSON_IsString(req_by)) {
                            tg_bot_send_text(req_by->valuestring, "✅ <b>Общая касса успешно обнулена!</b>");
                        }
                    }
                    // 5. Изменение цены за 1 час
                    else if (strcmp(evt, "SET_PRICE_1H") == 0) {
                        cJSON *val = cJSON_GetObjectItem(root, "value");
                        if (val && cJSON_IsNumber(val) && val->valueint > 0) {
                            system_state_set_price_per_1hour(val->valueint);

                            cJSON *req_by = cJSON_GetObjectItem(root, "requestedBy");
                            if (req_by && cJSON_IsString(req_by)) {
                                char msg_buf[128];
                                snprintf(msg_buf, sizeof(msg_buf),
                                         "✅ <b>Новая цена за 1 час:</b> %d сом", val->valueint);
                                tg_bot_send_text(req_by->valuestring, msg_buf);
                            }
                        }
                    }
                    // 6. Изменение минимального порога старта
                    else if (strcmp(evt, "SET_MIN_THRESHOLD") == 0) {
                        cJSON *val = cJSON_GetObjectItem(root, "value");
                        if (val && cJSON_IsNumber(val) && val->valueint > 0) {
                            system_state_set_min_threshold(val->valueint);

                            cJSON *req_by = cJSON_GetObjectItem(root, "requestedBy");
                            if (req_by && cJSON_IsString(req_by)) {
                                char msg_buf[128];
                                snprintf(msg_buf, sizeof(msg_buf),
                                         "✅ <b>Новый мин. порог старта:</b> %d сом", val->valueint);
                                tg_bot_send_text(req_by->valuestring, msg_buf);
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
    }
}

void cmd_handler_init(void) {
    if (s_mqtt_cmd_queue == NULL) {
        s_mqtt_cmd_queue = xQueueCreate(5, sizeof(mqtt_cmd_msg_t));
        xTaskCreate(mqtt_cmd_worker_task, "mqtt_cmd_worker", 4096, NULL, 4, NULL);
    }
}

void cmd_handler_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len) {
    if (!s_mqtt_cmd_queue || data_len <= 0) return;

    mqtt_cmd_msg_t msg;
    int copy_len = data_len < sizeof(msg.payload) - 1 ? data_len : sizeof(msg.payload) - 1;
    memcpy(msg.payload, data, copy_len);
    msg.payload[copy_len] = '\0';

    xQueueSend(s_mqtt_cmd_queue, &msg, 0);
}
