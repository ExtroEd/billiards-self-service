#include "system_state.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "Peripheral/display_tft.h"
#include "Peripheral/display_7seg.h"

static const char *TAG_SYS = "SYSTEM_STATE";
static const char *NVS_NAMESPACE = "storage";

static int g_balance = 0;
static int g_min_threshold_soms = 30; // 30 сом по умолчанию
// По умолчанию: 180 сом за 1 час
static int g_price_per_1hour = 180;
static int32_t g_total_money = 0;      // Касса
static int g_remaining_seconds = 0;
static bool g_relay_state = false;

static SemaphoreHandle_t g_state_mutex = NULL;

// Загрузка настроек из NVS
static void load_config_from_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        int32_t val32 = 0;
        if (nvs_get_i32(my_handle, "min_thresh", &val32) == ESP_OK) {
            g_min_threshold_soms = (int)val32;
        }
        if (nvs_get_i32(my_handle, "price_1h", &val32) == ESP_OK) {
            g_price_per_1hour = (int)val32;
        }
        if (nvs_get_i32(my_handle, "total_cash", &val32) == ESP_OK) {
            g_total_money = val32;
        }
        nvs_close(my_handle);
    }
}

// Сохранение настроек в NVS
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
            ESP_LOGW(TAG_SYS, "Время истекло. РЕЛЕ ВЫКЛЮЧЕНО!");
        }
    }
}

static void timer_countdown_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            if (g_relay_state && g_remaining_seconds > 0) {
                g_remaining_seconds--;

                int remaining_secs = g_remaining_seconds % 60;
                display_7seg_show_time(g_remaining_seconds / 60, remaining_secs, (g_remaining_seconds % 2 == 0));

                update_relay_state_unlocked();
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

    // Читаем сохраненные значения из NVS
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

    xTaskCreate(timer_countdown_task, "timer_countdown_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG_SYS, "Инициализация завершена. Порог: %d сом, Цена 1 час: %d сом", 
             g_min_threshold_soms, g_price_per_1hour);
}

void system_state_add_credit(int amount) {
    if (amount <= 0) return;

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_balance += amount;
        g_total_money += amount;

        save_config_to_nvs(); // Сохраняем кассу

        int added_seconds = convert_soms_to_seconds(amount);

        if (!g_relay_state) {
            if (g_balance >= g_min_threshold_soms) {
                g_remaining_seconds = convert_soms_to_seconds(g_balance);
                update_relay_state_unlocked();
            }
        } else {
            g_remaining_seconds += added_seconds;
            ESP_LOGI(TAG_SYS, "Дозачисление! Новое время: %d сек.", g_remaining_seconds);
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
    if (min_soms < 0) return;

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_min_threshold_soms = min_soms;
        save_config_to_nvs();
        xSemaphoreGive(g_state_mutex);
    }
}

int system_state_get_min_threshold(void) {
    int threshold = 30;
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
