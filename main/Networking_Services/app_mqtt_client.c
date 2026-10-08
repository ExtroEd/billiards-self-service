#include "app_mqtt_client.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "MQTT_CLIENT";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_is_connected = false;

extern void system_state_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len);

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Успешно подключились к EMQX!");
        s_is_connected = true;

        // 1. Динамическая личная подписка стола (например: billiards/klubnyy15_table4/cmd)
        char personal_cmd_topic[64];
        snprintf(personal_cmd_topic, sizeof(personal_cmd_topic), "billiards/klubnyy15_table%d/cmd", TABLE_NUMBER);
        esp_mqtt_client_subscribe(s_client, personal_cmd_topic, 1);
        
        // 2. Общая подписка для всех столов
        esp_mqtt_client_subscribe(s_client, "billiards/all/cmd", 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Отключились от MQTT брокера");
        s_is_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Подписка подтверждена, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, ">>> [MQTT RX] Получены данные из EMQX!");
        ESP_LOGI(TAG, ">>> Topic (длина %d): %.*s", event->topic_len, event->topic_len, event->topic);
        ESP_LOGI(TAG, ">>> Payload (длина %d): %.*s", event->data_len, event->data_len, event->data);
        ESP_LOGI(TAG, "===============================================");

        system_state_handle_mqtt_cmd(event->topic, event->topic_len, event->data, event->data_len);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Ошибка MQTT");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "Ошибка TLS/TCP: 0x%x", event->error_handle->esp_tls_last_esp_err);
        }
        break;

    default:
        break;
    }
}

esp_err_t mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials = {
            .username = MQTT_USER,
            .authentication = {
                .password = MQTT_PASS,
            },
        },
        .task.stack_size = 6144,
        .task.priority = 5,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Не удалось инициализировать MQTT клиент");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

bool mqtt_app_is_connected(void)
{
    return s_is_connected;
}

esp_err_t mqtt_app_publish_status(const char *payload)
{
    if (!s_is_connected || s_client == NULL) {
        ESP_LOGW(TAG, "Невозможно отправить данные: нет подключения");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_STATUS, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Ошибка публикации сообщения");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Сообщение отправлено в %s, msg_id=%d", MQTT_TOPIC_STATUS, msg_id);
    return ESP_OK;
}
