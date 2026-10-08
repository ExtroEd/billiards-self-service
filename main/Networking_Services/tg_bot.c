#include "tg_bot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include "secrets.h"
#include "system_state.h"

static const char *TAG = "TG_BOT";

typedef struct {
    char chat_id[64];
} tg_task_param_t;

void tg_bot_send_text(const char *chat_id, const char *text) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", TG_BOT_TOKEN);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "parse_mode", "HTML");

    // Нижная текстовая клавиатура для быстрого запроса отчёта
    cJSON *reply_markup = cJSON_CreateObject();
    cJSON *keyboard = cJSON_CreateArray();
    cJSON *row = cJSON_CreateArray();

    cJSON *button = cJSON_CreateObject();
    cJSON_AddStringToObject(button, "text", "📊 Получить информацию");
    cJSON_AddItemToArray(row, button);

    cJSON_AddItemToArray(keyboard, row);
    cJSON_AddItemToObject(reply_markup, "keyboard", keyboard);
    cJSON_AddBoolToObject(reply_markup, "resize_keyboard", true);

    cJSON_AddItemToObject(root, "reply_markup", reply_markup);

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Сообщение успешно отправлено в Telegram!");
    } else {
        ESP_LOGE(TAG, "Ошибка отправки в Telegram: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_body);
}

static void tg_async_report_task(void *pvParameters) {
    tg_task_param_t *param = (tg_task_param_t *)pvParameters;
    if (param) {
        char report[400];

        bool relay_active = system_state_is_relay_active();
        int32_t total_cash = system_state_get_total_money();
        int price_1h = system_state_get_price_per_1hour();
        int min_th = system_state_get_min_threshold();
        int rem_sec = system_state_get_remaining_seconds();

        // ❌ Строка с "Текущим балансом" удалена
        snprintf(report, sizeof(report),
                 "<b>🎱 Отчёт: Стол №%d</b>\n\n"
                 "%s <b>Статус света:</b> %s\n"
                 "⏱️ <b>Остаток времени:</b> %02d мин %02d сек\n"
                 "💰 <b>Касса (всего):</b> %ld сом\n"
                 "⚙️ <b>Тариф за 1 час:</b> %d сом\n"
                 "🎯 <b>Мин. старт:</b> %d сом",
                 TABLE_NUMBER,
                 relay_active ? "🟢" : "🔴",
                 relay_active ? "ВКЛЮЧЁН (Идёт игра)" : "ВЫКЛЮЧЁН (Свободен)",
                 rem_sec / 60, rem_sec % 60,
                 (long)total_cash,
                 price_1h,
                 min_th);

        tg_bot_send_text(param->chat_id, report);
        free(param);
    }
    vTaskDelete(NULL);
}

void tg_bot_send_status_report(const char *target_chat_id) {
    if (!target_chat_id) return;

    tg_task_param_t *param = malloc(sizeof(tg_task_param_t));
    if (!param) return;

    strncpy(param->chat_id, target_chat_id, sizeof(param->chat_id) - 1);
    param->chat_id[sizeof(param->chat_id) - 1] = '\0';

    xTaskCreate(tg_async_report_task, "tg_send_task", 6144, param, 3, NULL);
}
