#include "system_state.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_sntp.h"
#include "Networking_Services/tg_bot.h"

#include "Peripheral/display_tft.h"
#include "Peripheral/display_7seg.h"
#include "Peripheral/ds3231.h"

#define MAX_OFFLINE_SECONDS (2 * 3600) // 2 часа = 7200 секунд

// Минимальный валидный timestamp (например, 01.01.2024 00:00:00 UTC)
#ifndef MIN_VALID_TIMESTAMP
#define MIN_VALID_TIMESTAMP 1704067200LL
#endif

// Максимально допустимое время простоя без сброса (например, 2 часа = 7200 сек)
#ifndef MAX_OFFLINE_SECONDS
#define MAX_OFFLINE_SECONDS 7200LL
#endif

static const char *TAG_SYS = "SYSTEM_STATE";
static const char *NVS_NAMESPACE = "storage";

static int g_balance = 0;
static int g_min_threshold_soms = 1; 
static int g_price_per_1hour = 180;   
static int32_t g_total_money = 0;      
static int g_remaining_seconds = 0;
static bool g_relay_state = false;

static i2c_master_dev_handle_t s_ds3231_dev = NULL;
static SemaphoreHandle_t g_state_mutex = NULL;
static QueueHandle_t g_mqtt_cmd_queue = NULL;

typedef struct {
    char payload[512];
} mqtt_cmd_msg_t;

// Коллбэк успешной синхронизации времени по Wi-Fi (SNTP -> DS3231)
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG_SYS, "SNTP время синхронизировано с сервером!");
    if (s_ds3231_dev && tv) {
        if (ds3231_set_time(s_ds3231_dev, (int64_t)tv->tv_sec) == ESP_OK) {
            ESP_LOGI(TAG_SYS, "DS3231 успешно синхронизирован с NTP!");
        }
    }
}

void system_state_init_sntp(void) {
    ESP_LOGI(TAG_SYS, "Инициализация SNTP...");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

static void load_config_from_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        int32_t val32 = 0;

        if (nvs_get_i32(my_handle, "min_thresh", &val32) == ESP_OK && val32 >= 1) {
            g_min_threshold_soms = (int)val32;
        }
        if (nvs_get_i32(my_handle, "price_1h", &val32) == ESP_OK && val32 >= 1) {
            g_price_per_1hour = (int)val32;
        }
        if (nvs_get_i32(my_handle, "total_cash", &val32) == ESP_OK) {
            g_total_money = val32;
        }
        nvs_close(my_handle);
    }
}

static void save_config_to_nvs(int32_t total_cash) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_i32(my_handle, "min_thresh", g_min_threshold_soms);
        nvs_set_i32(my_handle, "price_1h", g_price_per_1hour);
        nvs_set_i32(my_handle, "total_cash", total_cash);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

static int convert_soms_to_seconds(int soms) {
    if (g_price_per_1hour <= 0) return 0;
    return (soms * 3600) / g_price_per_1hour;
}

static void update_relay_state_unlocked(void) {
    if (g_remaining_seconds > 0) {
        if (!g_relay_state) {
            g_relay_state = true;
            gpio_set_level(RELAY_GPIO_PIN, 1);
            ESP_LOGI(TAG_SYS, "РЕЛЕ ВКЛЮЧЕНО! Оставшееся время: %d сек.", g_remaining_seconds);
        }
    } else {
        if (g_relay_state) {
            g_relay_state = false;
            g_balance = 0;
            g_remaining_seconds = 0;
            if (s_ds3231_dev) {
                ds3231_write_rem_seconds(s_ds3231_dev, 0);
            }
            gpio_set_level(RELAY_GPIO_PIN, 0);
            ESP_LOGW(TAG_SYS, "Время истекло. РЕЛЕ ВЫКЛЮЧЕНО!");
        }
    }
}

static void timer_countdown_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (g_relay_state && g_remaining_seconds > 0) {
                g_remaining_seconds--;

                if (s_ds3231_dev) {
                    ds3231_write_rem_seconds(s_ds3231_dev, (int32_t)g_remaining_seconds);
                    
                    // Также обновляем метку времени последнего активного тика
                    int64_t current_ts = 0;
                    if (ds3231_get_time(s_ds3231_dev, &current_ts) == ESP_OK) {
                        ds3231_write_last_timestamp(s_ds3231_dev, current_ts);
                    }
                }

                int remaining_secs = g_remaining_seconds % 60;
                display_7seg_show_time(g_remaining_seconds / 60, remaining_secs, (g_remaining_seconds % 2 == 0));

                update_relay_state_unlocked();
            } else if (!g_relay_state) {
                display_7seg_snake_step();
            }
            xSemaphoreGive(g_state_mutex);
        }
    }
}

static void mqtt_cmd_worker_task(void *pvParameters) {
    mqtt_cmd_msg_t msg;

    while (1) {
        if (xQueueReceive(g_mqtt_cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG_SYS, "=== [MQTT WORKER] Разбор входящего сообщения ===");

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
                        ESP_LOGW(TAG_SYS, "Удаленный сброс сессии выполнен!");
                        
                        cJSON *req_by = cJSON_GetObjectItem(root, "requestedBy");
                        if (req_by && cJSON_IsString(req_by)) {
                            tg_bot_send_text(req_by->valuestring, "✅ <b>Сессия и время успешно сброшены!</b>");
                        }
                    }
                    // 4. Сброс общей кассы (Инкассация)
                    else if (strcmp(evt, "RESET_CASH") == 0) {
                        system_state_reset_total_money();
                        ESP_LOGW(TAG_SYS, "Удаленная инкассация (сброс кассы) выполнена!");

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

void system_state_init(i2c_master_dev_handle_t ds3231_dev) {
    s_ds3231_dev = ds3231_dev;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_state_mutex = xSemaphoreCreateMutex();
    g_mqtt_cmd_queue = xQueueCreate(5, sizeof(mqtt_cmd_msg_t));

    g_balance = 0;
    g_remaining_seconds = 0;
    g_relay_state = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_GPIO_PIN, 0);

    display_7seg_init();

    setenv("TZ", "KGT-6", 1);
    tzset();

    load_config_from_nvs();

    int32_t saved_secs = 0;
    int64_t last_ts = 0;
    int64_t now_ts = 0;

    if (s_ds3231_dev &&
        ds3231_read_rem_seconds(s_ds3231_dev, &saved_secs) == ESP_OK && saved_secs > 0) 
    {
        // Если есть сохраненные секунды, первично считаем, что сессия валидна
        bool should_restore = true;

        // Проверяем метки времени, только если ОБЕ метки адекватные (> MIN_VALID_TIMESTAMP)
        if (ds3231_read_last_timestamp(s_ds3231_dev, &last_ts) == ESP_OK &&
            ds3231_get_time(s_ds3231_dev, &now_ts) == ESP_OK) 
        {
            if (last_ts >= MIN_VALID_TIMESTAMP && now_ts >= MIN_VALID_TIMESTAMP) {
                int64_t offline_duration = now_ts - last_ts;
                ESP_LOGI(TAG_SYS, "Проверка времени простоя: прошлый=%lld, сейчас=%lld, пропущено=%lld сек.", 
                        last_ts, now_ts, offline_duration);

                if (offline_duration < 0 || offline_duration > MAX_OFFLINE_SECONDS) {
                    ESP_LOGW(TAG_SYS, "⚠️ Питание было отключено слишком долго (%lld сек.)! Сброс.", offline_duration);
                    should_restore = false;
                }
            } else {
                ESP_LOGW(TAG_SYS, "⚠️ Время RTC еще не синхронизировано (last=%lld, now=%lld). Доверяем сохраненным секундам.", last_ts, now_ts);
            }
        }

        if (should_restore) {
            g_remaining_seconds = (int)saved_secs;
            g_relay_state = true;
            gpio_set_level(RELAY_GPIO_PIN, 1);
            ESP_LOGI(TAG_SYS, "🔥 ВОССТАНОВЛЕНИЕ СЕССИИ ИЗ DS3231! Замороженный остаток: %d сек.", g_remaining_seconds);
        } else {
            g_remaining_seconds = 0;
            g_relay_state = false;
            gpio_set_level(RELAY_GPIO_PIN, 0);
            ds3231_write_rem_seconds(s_ds3231_dev, 0);
        }
    }

    xTaskCreate(timer_countdown_task, "timer_countdown_task", 3072, NULL, 5, NULL);
    xTaskCreate(mqtt_cmd_worker_task, "mqtt_cmd_worker", 4096, NULL, 4, NULL);
}

void system_state_add_credit(int amount) {
    if (amount <= 0) return;

    int32_t current_total_cash = 0;

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_balance += amount;
        g_total_money += amount;

        int added_seconds = convert_soms_to_seconds(amount);

        if (!g_relay_state) {
            if (g_balance >= g_min_threshold_soms) {
                g_remaining_seconds = convert_soms_to_seconds(g_balance);
                if (s_ds3231_dev) {
                    ds3231_write_rem_seconds(s_ds3231_dev, (int32_t)g_remaining_seconds);
                    int64_t current_ts = 0;
                    if (ds3231_get_time(s_ds3231_dev, &current_ts) == ESP_OK) {
                        ds3231_write_last_timestamp(s_ds3231_dev, current_ts);
                    }
                }
                update_relay_state_unlocked();
            }
        } else {
            g_remaining_seconds += added_seconds;
            if (s_ds3231_dev) {
                ds3231_write_rem_seconds(s_ds3231_dev, (int32_t)g_remaining_seconds);
            }
        }

        current_total_cash = g_total_money;
        xSemaphoreGive(g_state_mutex);
    }

    display_tft_wake();
    save_config_to_nvs(current_total_cash);
}

void system_state_reset_balance(void) {
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_balance = 0;
        g_remaining_seconds = 0;
        if (s_ds3231_dev) {
            ds3231_write_rem_seconds(s_ds3231_dev, 0);
        }
        update_relay_state_unlocked();
        display_7seg_clear();
        xSemaphoreGive(g_state_mutex);
    }
}

int system_state_get_balance(void) {
    int current_balance = 0;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        current_balance = g_balance;
        xSemaphoreGive(g_state_mutex);
    }
    return current_balance;
}

int system_state_get_remaining_seconds(void) {
    int rem = 0;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        rem = g_remaining_seconds;
        xSemaphoreGive(g_state_mutex);
    }
    return rem;
}

void system_state_set_min_threshold(int min_soms) {
    if (min_soms < 1) return;
    int32_t total_cash = 0;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_min_threshold_soms = min_soms;
        total_cash = g_total_money;
        xSemaphoreGive(g_state_mutex);
    }
    save_config_to_nvs(total_cash);
}

int system_state_get_min_threshold(void) {
    int threshold = 1;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        threshold = g_min_threshold_soms;
        xSemaphoreGive(g_state_mutex);
    }
    return threshold;
}

void system_state_set_price_per_1hour(int price) {
    if (price <= 0) return;
    int32_t total_cash = 0;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_price_per_1hour = price;
        total_cash = g_total_money;
        xSemaphoreGive(g_state_mutex);
    }
    save_config_to_nvs(total_cash);
}

int system_state_get_price_per_1hour(void) {
    int price = 180;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        price = g_price_per_1hour;
        xSemaphoreGive(g_state_mutex);
    }
    return price;
}

int32_t system_state_get_total_money(void) {
    int32_t total = 0;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        total = g_total_money;
        xSemaphoreGive(g_state_mutex);
    }
    return total;
}

void system_state_reset_total_money(void) {
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_total_money = 0;
        xSemaphoreGive(g_state_mutex);
    }
    save_config_to_nvs(0);
}

bool system_state_is_relay_active(void) {
    bool active = false;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        active = g_relay_state;
        xSemaphoreGive(g_state_mutex);
    }
    return active;
}

void system_state_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len) {
    if (!g_mqtt_cmd_queue || data_len <= 0) return;

    mqtt_cmd_msg_t msg;
    int copy_len = data_len < sizeof(msg.payload) - 1 ? data_len : sizeof(msg.payload) - 1;
    memcpy(msg.payload, data, copy_len);
    msg.payload[copy_len] = '\0';

    xQueueSend(g_mqtt_cmd_queue, &msg, 0);
}
