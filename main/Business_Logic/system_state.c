#include "system_state.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_sntp.h"

#include "Peripheral/display_tft.h"
#include "Peripheral/display_7seg.h"

static const char *TAG_SYS = "SYSTEM_STATE";
static const char *NVS_NAMESPACE = "storage";

static int g_balance = 0;
static int g_min_threshold_soms = 1; // 1 сом по умолчанию
static int g_price_per_1hour = 180;   // 180 сом за 1 час
static int32_t g_total_money = 0;      // Касса
static int g_remaining_seconds = 0;
static bool g_relay_state = false;

static SemaphoreHandle_t g_state_mutex = NULL;

// --- Инициализация NTP (Сетевого времени) ---
void system_state_init_sntp(void) {
    ESP_LOGI(TAG_SYS, "Инициализация SNTP...");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
}

// --- NVS: Сохранение оставшегося времени в секундах ---
static void save_remaining_seconds_to_nvs(int seconds) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_i32(my_handle, "rem_sec", seconds);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

// --- NVS: Загрузка настроек ---
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

// --- NVS: Сохранение настроек ---
static void save_config_to_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_i32(my_handle, "min_thresh", g_min_threshold_soms);
        nvs_set_i32(my_handle, "price_1h", g_price_per_1hour);
        nvs_set_i32(my_handle, "total_cash", g_total_money);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

// Перерасчёт времени: N сом * 3600 сек / цена_за_1_час
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
            gpio_set_level(RELAY_GPIO_PIN, 0);
            save_remaining_seconds_to_nvs(0); // Сбрасываем NVS
            ESP_LOGW(TAG_SYS, "Время истекло. РЕЛЕ ВЫКЛЮЧЕНО!");
        }
    }
}

static void timer_countdown_task(void *pvParameters) {
    int save_counter = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            if (g_relay_state && g_remaining_seconds > 0) {
                g_remaining_seconds--;

                int remaining_secs = g_remaining_seconds % 60;
                display_7seg_show_time(g_remaining_seconds / 60, remaining_secs, (g_remaining_seconds % 2 == 0));

                update_relay_state_unlocked();

                // Сохраняем в NVS каждые 5 секунд (чтобы не изнашивать flash лишней записью)
                if (++save_counter >= 5) {
                    save_counter = 0;
                    save_remaining_seconds_to_nvs(g_remaining_seconds);
                }
            } else if (!g_relay_state) {
                display_7seg_clear();
            }
            xSemaphoreGive(g_state_mutex);
        }
    }
}

void system_state_init(void) {
    // Инициализация NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_state_mutex = xSemaphoreCreateMutex();
    g_balance = 0;
    g_remaining_seconds = 0;
    g_relay_state = false;

    // Читаем сохраненные настройки из NVS
    load_config_from_nvs();

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

    // Устанавливаем часовой пояс (UTC+6)
    setenv("TZ", "KGT-6", 1);
    tzset();

    // --- Восстановление оставшегося времени в секундах после перезагрузки ---
    nvs_handle_t my_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle) == ESP_OK) {
        int32_t saved_seconds = 0;
        if (nvs_get_i32(my_handle, "rem_sec", &saved_seconds) == ESP_OK && saved_seconds > 0) {
            g_remaining_seconds = (int)saved_seconds;
            update_relay_state_unlocked();
            ESP_LOGI(TAG_SYS, "Сессия восстановлена! Осталось: %d сек.", g_remaining_seconds);
        }
        nvs_close(my_handle);
    }

    xTaskCreate(timer_countdown_task, "timer_countdown_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG_SYS, "Инициализация завершена. Порог: %d сом, Цена 1 час: %d сом", 
             g_min_threshold_soms, g_price_per_1hour);
}

void system_state_add_credit(int amount) {
    if (amount <= 0) {
        ESP_LOGW(TAG_SYS, "system_state_add_credit: Отклонено (сумма <= 0: %d)", amount);
        return;
    }

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG_SYS, "--> [ADD CREDIT] Поступление: %d сом. Старый баланс: %d, Старая касса: %ld", 
                 amount, g_balance, (long)g_total_money);

        g_balance += amount;
        g_total_money += amount;

        save_config_to_nvs(); // Сохраняем кассу в NVS

        int added_seconds = convert_soms_to_seconds(amount);
        ESP_LOGI(TAG_SYS, "--> [ADD CREDIT] Рассчитано добавочное время: %d сек. (за %d сом)", added_seconds, amount);

        if (!g_relay_state) {
            ESP_LOGI(TAG_SYS, "--> [ADD CREDIT] Реле ВЫКЛ. Текущий баланс (%d) vs Порог (%d)", g_balance, g_min_threshold_soms);
            if (g_balance >= g_min_threshold_soms) {
                g_remaining_seconds = convert_soms_to_seconds(g_balance);
                ESP_LOGI(TAG_SYS, "--> [ADD CREDIT] Порог пройден! Старт таймера на %d сек.", g_remaining_seconds);
                update_relay_state_unlocked();
            } else {
                ESP_LOGW(TAG_SYS, "--> [ADD CREDIT] Недостаточно средств для порога (%d < %d)", g_balance, g_min_threshold_soms);
            }
        } else {
            g_remaining_seconds += added_seconds;
            ESP_LOGI(TAG_SYS, "--> [ADD CREDIT] Дозачисление! Новое время: %d сек.", g_remaining_seconds);
        }

        // Сохраняем актуальный остаток в NVS
        if (g_remaining_seconds > 0) {
            save_remaining_seconds_to_nvs(g_remaining_seconds);
        }

        display_tft_wake();
        xSemaphoreGive(g_state_mutex);
    }
}

void system_state_reset_balance(void) {
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_balance = 0;
        g_remaining_seconds = 0;
        update_relay_state_unlocked();
        save_remaining_seconds_to_nvs(0);
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

void system_state_set_min_threshold(int min_soms) {
    if (min_soms < 1) return;

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_min_threshold_soms = min_soms;
        save_config_to_nvs();
        xSemaphoreGive(g_state_mutex);
    }
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

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_price_per_1hour = price;
        save_config_to_nvs();
        xSemaphoreGive(g_state_mutex);
    }
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
        save_config_to_nvs();
        xSemaphoreGive(g_state_mutex);
    }
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
    ESP_LOGI(TAG_SYS, "=== [MQTT CMD PARSER] Начало обработки сообщения ===");
    ESP_LOGI(TAG_SYS, "Принятый топик: %.*s", topic_len, topic);
    ESP_LOGI(TAG_SYS, "Сырое тело (Payload): %.*s", data_len, data);

    char *json_buf = malloc(data_len + 1);
    if (!json_buf) {
        ESP_LOGE(TAG_SYS, "[ERROR] Не удалось выделить память под json_buf!");
        return;
    }
    memcpy(json_buf, data, data_len);
    json_buf[data_len] = '\0';

    cJSON *root = cJSON_Parse(json_buf);
    if (root != NULL) {
        cJSON *event = cJSON_GetObjectItem(root, "event");
        cJSON *amount = cJSON_GetObjectItem(root, "amount");

        if (event && cJSON_IsString(event)) {
            ESP_LOGI(TAG_SYS, "Поле 'event': '%s'", event->valuestring);
        } else {
            ESP_LOGW(TAG_SYS, "[WARN] Поле 'event' отсутствует или не является строкой");
        }

        if (amount && cJSON_IsNumber(amount)) {
            ESP_LOGI(TAG_SYS, "Поле 'amount': %d", amount->valueint);
        } else {
            ESP_LOGW(TAG_SYS, "[WARN] Поле 'amount' отсутствует или не является числом");
        }

        if (event && cJSON_IsString(event) && (strcmp(event->valuestring, "PAYMENT_SUCCESS") == 0)) {
            if (amount && cJSON_IsNumber(amount)) {
                int credit = amount->valueint;
                ESP_LOGI(TAG_SYS, ">>> УСПЕХ: Начисление зачислено из MQTT! Сумма: %d сом", credit);
                system_state_add_credit(credit);
            } else {
                ESP_LOGE(TAG_SYS, "[ERROR] Событие PAYMENT_SUCCESS, но сумма 'amount' невалидна!");
            }
        } else {
            ESP_LOGW(TAG_SYS, "[WARN] 'event' не равен 'PAYMENT_SUCCESS'. Пропуск обработки.");
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE(TAG_SYS, "[ERROR] cJSON_Parse не смог распарсить JSON! Ошибка синтаксиса.");
    }
    free(json_buf);
    ESP_LOGI(TAG_SYS, "=== [MQTT CMD PARSER] Завершение обработки ===");
}
