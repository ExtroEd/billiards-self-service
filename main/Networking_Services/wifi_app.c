#include "wifi_app.h"
#include "web_server_app.h"
#include "app_mqtt_client.h"
#include "display_tft.h"
#include "secrets.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "system_state.h"

static const char *TAG = "WIFI_APP";

static esp_timer_handle_t s_ap_timeout_timer = NULL;
#define AP_TIMEOUT_MS (3 * 60 * 1000) // 3 минуты

#define NVS_NAMESPACE      "wifi_config"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

#define AP_SSID            "ESP32_Config"
#define AP_PASS            "12345678"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static wifi_app_mode_t s_current_mode = WIFI_APP_MODE_NONE;
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_sta = NULL;
static int s_retry_num = 0;

static void stop_ap_and_reconnect_sta(void);
static bool connect_to_saved_wifi(const char *ssid, const char *pass);

static void ap_timeout_callback(void* arg) {
    ESP_LOGW(TAG, "Таймаут SoftAP (3 минуты истекли). Возврат к основному Wi-Fi...");
    stop_ap_and_reconnect_sta();
}

static void start_ap_timeout_timer(void) {
    if (s_ap_timeout_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &ap_timeout_callback,
            .name = "ap_timeout"
        };
        esp_timer_create(&timer_args, &s_ap_timeout_timer);
    }
    esp_timer_stop(s_ap_timeout_timer);
    esp_timer_start_once(s_ap_timeout_timer, AP_TIMEOUT_MS * 1000LL);
    ESP_LOGI(TAG, "Таймер отключения SoftAP запущен на 3 минуты");
}

static void stop_ap_timeout_timer(void) {
    if (s_ap_timeout_timer != NULL) {
        esp_timer_stop(s_ap_timeout_timer);
    }
}

void wifi_app_stop_ap_and_reconnect(void) {
    stop_ap_and_reconnect_sta();
}

static void stop_ap_and_reconnect_sta(void) {
    stop_ap_timeout_timer();
    web_server_app_stop();

    char ssid[32] = {0}, pass[64] = {0};
    if (wifi_app_read_credentials(ssid, pass) == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Возврат к сети из NVS: %s", ssid);
        connect_to_saved_wifi(ssid, pass);
    } else {
        ESP_LOGI(TAG, "Возврат к дефолтной сети: %s", SECRET_WIFI_SSID);
        connect_to_saved_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
    }
}

esp_err_t wifi_app_save_credentials(const char *ssid, const char *pass) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t wifi_app_read_credentials(char *ssid_out, char *pass_out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t ssid_len = 32;
    size_t pass_len = 64;

    err = nvs_get_str(handle, NVS_KEY_SSID, ssid_out, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_KEY_PASS, pass_out, &pass_len);
    }
    nvs_close(handle);
    return err;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Смартфон подключился к SoftAP (AID: %d). Старт 3-минутного таймера.", event->aid);

        start_ap_timeout_timer();
        display_tft_wake();
        display_tft_show_qr_payload("http://192.168.4.1/", "САЙТ");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Смартфон отключился от SoftAP. Возврат к рабочему Wi-Fi...");
        stop_ap_and_reconnect_sta();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Попытка переподключения к Wi-Fi #%d...", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Успешно получен IP-адрес: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_retry_num = 0;
        system_state_init_sntp();
        
        ESP_LOGI(TAG, "Запуск MQTT клиента...");
        mqtt_app_start();

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void start_soft_ap(void) {
    s_current_mode = WIFI_APP_MODE_AP;
    esp_wifi_stop();

    if (s_netif_ap == NULL) {
        s_netif_ap = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = AP_PASS,
            .max_connection = 4,
            .authmode = (strlen(AP_PASS) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    web_server_app_start();
    start_ap_timeout_timer();
}

static bool connect_to_saved_wifi(const char *ssid, const char *pass) {
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
    } else {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }

    s_retry_num = 0;

    if (s_netif_sta == NULL) {
        s_netif_sta = esp_netif_create_default_wifi_sta();
    }

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT) {
        s_current_mode = WIFI_APP_MODE_STA;
        return true;
    }
    
    return false;
}

void wifi_app_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    char ssid[32] = {0}, pass[64] = {0};

    if (wifi_app_read_credentials(ssid, pass) == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Найдены настройки Wi-Fi в NVS: %s", ssid);
        if (!connect_to_saved_wifi(ssid, pass)) {
            ESP_LOGW(TAG, "Не удалось подключиться к сохраненной сети NVS. Запуск SoftAP...");
            start_soft_ap();
        }
    } else {
        ESP_LOGI(TAG, "Настройки NVS пустые. Пробуем сети по умолчанию из secrets.h (%s)...", SECRET_WIFI_SSID);
        
        if (connect_to_saved_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS)) {
            ESP_LOGI(TAG, "Успешное подключение по умолчанию! Сохраняем в NVS...");
            wifi_app_save_credentials(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
        } else {
            ESP_LOGW(TAG, "Не удалось подключиться к дефолтной сети. Запуск SoftAP...");
            start_soft_ap();
        }
    }
}

bool wifi_app_is_connected(void) {
    return (s_current_mode == WIFI_APP_MODE_STA);
}

void wifi_app_start_ap_mode(void) {
    start_soft_ap();
}

wifi_app_mode_t wifi_app_get_mode(void) {
    return s_current_mode;
}
