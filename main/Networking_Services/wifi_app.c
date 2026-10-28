#include "wifi_app.h"
#include "app_mqtt_client.h"
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
#include "system_state.h"

static const char *TAG = "WIFI_APP";

#define NVS_NAMESPACE      "wifi_config"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static wifi_app_mode_t s_current_mode = WIFI_APP_MODE_NONE;
static esp_netif_t *s_netif_sta = NULL;
static int s_retry_num = 0;

static bool connect_to_wifi(const char *ssid, const char *pass);

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_current_mode = WIFI_APP_MODE_NONE;
        if (s_retry_num < 20) { // Пробуем переподключиться до 20 раз
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Попытка переподключения к Wi-Fi #%d...", s_retry_num);
        } else {
            ESP_LOGE(TAG, "Не удалось переподключиться к Wi-Fi. Установка бита сбоя.");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Успешно получен IP-адрес: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_retry_num = 0;
        s_current_mode = WIFI_APP_MODE_STA;
        
        system_state_init_sntp();
        
        ESP_LOGI(TAG, "Запуск MQTT клиента...");
        mqtt_app_start();

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool connect_to_wifi(const char *ssid, const char *pass) {
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
            pdFALSE, pdFALSE, portMAX_DELAY); // Ждем до победного подключения

    return (bits & WIFI_CONNECTED_BIT) != 0;
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

    // Сразу используем данные из secrets.h для стабильной работы стационарного устройства
    ESP_LOGI(TAG, "Подключение к Wi-Fi сети из secrets.h (%s)...", SECRET_WIFI_SSID);
    
    if (connect_to_wifi(SECRET_WIFI_SSID, SECRET_WIFI_PASS)) {
        ESP_LOGI(TAG, "Wi-Fi успешно подключен!");
    } else {
        ESP_LOGE(TAG, "Критическая ошибка: не удалось подключиться к Wi-Fi.");
    }
}

bool wifi_app_is_connected(void) {
    return (s_current_mode == WIFI_APP_MODE_STA);
}

wifi_app_mode_t wifi_app_get_mode(void) {
    return s_current_mode;
}
