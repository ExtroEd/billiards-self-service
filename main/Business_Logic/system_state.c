#include "system_state.h"
#include "cmd_handler.h"
#include "Networking_Services/wifi_app.h"
#include "Networking_Services/app_mqtt_client.h"
#include "Peripheral/display_tft.h"
#include "Peripheral/display_7seg.h"
#include "Peripheral/ds3231.h"

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"

#define MAX_OFFLINE_SECONDS (2 * 3600) // 2 часа = 7200 секунд

#ifndef MIN_VALID_TIMESTAMP
#define MIN_VALID_TIMESTAMP 1704067200LL
#endif

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
    uint32_t ms_accumulator = 0;
    const uint32_t step_ms = 150; // Скорость анимации змейки = 150 мс
    bool snake_was_active = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        ms_accumulator += step_ms;

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            
            // 1. АНИМАЦИЯ ЗМЕЙКИ (Только когда реле выключено и есть Wi-Fi + MQTT)
            if (!g_relay_state) {
                bool network_ok = wifi_app_is_connected() && mqtt_app_is_connected();

                if (network_ok) {
                    display_7seg_snake_step();
                    snake_was_active = true;
                } else {
                    if (snake_was_active) {
                        display_7seg_clear();
                        snake_was_active = false;
                    }
                }
            }

            // 2. ОБРАБОТКА 1 СЕКУНДЫ (ровно раз в 1000 мс)
            if (ms_accumulator >= 1000) {
                ms_accumulator -= 1000;

                if (g_relay_state && g_remaining_seconds > 0) {
                    g_remaining_seconds--;

                    if (s_ds3231_dev) {
                        ds3231_write_rem_seconds(s_ds3231_dev, (int32_t)g_remaining_seconds);
                        
                        int64_t current_ts = 0;
                        if (ds3231_get_time(s_ds3231_dev, &current_ts) == ESP_OK) {
                            ds3231_write_last_timestamp(s_ds3231_dev, current_ts);
                        }
                    }

                    int remaining_secs = g_remaining_seconds % 60;
                    display_7seg_show_time(g_remaining_seconds / 60, remaining_secs, (g_remaining_seconds % 2 == 0));

                    update_relay_state_unlocked();
                }
            }

            xSemaphoreGive(g_state_mutex);
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

    // Инициализируем обработчик MQTT/Telegram команд
    cmd_handler_init();

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
        bool should_restore = true;

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

bool system_state_is_rtc_ok(void) {
    bool ok = false;
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_ds3231_dev != NULL) {
            int64_t dummy_ts = 0;
            ok = (ds3231_get_time(s_ds3231_dev, &dummy_ts) == ESP_OK);
        }
        xSemaphoreGive(g_state_mutex);
    }
    return ok;
}

void system_state_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len) {
    cmd_handler_handle_mqtt_cmd(topic, topic_len, data, data_len);
}
