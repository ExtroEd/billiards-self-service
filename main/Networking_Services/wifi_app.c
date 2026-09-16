#include "wifi_app.h"
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
#include "esp_http_server.h"
#include "lwip/sockets.h"

static const char *TAG = "WIFI_APP";

#define NVS_NAMESPACE      "wifi_config"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

#define AP_SSID            "Terminal-Setup"
#define AP_PASS            "12345678" // Можно оставить пустым "" если без пароля

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static wifi_app_mode_t s_current_mode = WIFI_APP_MODE_NONE;
static httpd_handle_t s_http_server = NULL;
static int s_retry_num = 0;

// HTML-страница веб-кабинета (сохранена в flash-память)
static const char *HTML_FORM = 
    "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>body{font-family:Arial;background:#1a1a1a;color:#fff;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
    ".card{background:#2a2a2a;padding:25px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.5);width:85%%;max-width:350px;}"
    "h2{margin-top:0;color:#00d2ff;text-align:center;}input{width:100%%;padding:10px;margin:8px 0 16px 0;border:none;border-radius:6px;box-sizing:border-box;}"
    "button{width:100%%;background:#00d2ff;color:#000;font-weight:bold;padding:12px;border:none;border-radius:6px;cursor:pointer;}"
    "</style></head><body><div class=\"card\"><h2>Terminal Wi-Fi</h2>"
    "<form action=\"/save\" method=\"POST\">"
    "<label>SSID (Название Wi-Fi):</label><input type=\"text\" name=\"ssid\" required>"
    "<label>Пароль:</label><input type=\"password\" name=\"pass\">"
    "<button type=\"submit\">Сохранить и перезагрузить</button>"
    "</form></div></body></html>";

// --- NVS ФУНКЦИИ ---

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

// --- ОБРАБОТЧИКИ HTTP СЕРВЕРА ---

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    char ssid[32] = {0};
    char pass[64] = {0};

    // Простой парсинг URL-encoded данных: ssid=...&pass=...
    char *ssid_ptr = strstr(buf, "ssid=");
    char *pass_ptr = strstr(buf, "pass=");

    if (ssid_ptr) {
        sscanf(ssid_ptr, "ssid=%31[^&]", ssid);
    }
    if (pass_ptr) {
        sscanf(pass_ptr, "pass=%63s", pass);
    }

    ESP_LOGI(TAG, "Received new Wi-Fi credentials: SSID='%s'", ssid);
    wifi_app_save_credentials(ssid, pass);

    const char *resp = "<html><body><h2>Настройки сохранены! Терминал перезагружается...</h2></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart(); // Перезагружаем устройство для подключения к роутеру

    return ESP_OK;
}

static void start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri      = "/",
            .method   = HTTP_GET,  // Было HTTPD_GET -> стало HTTP_GET
            .handler  = root_get_handler
        };
        httpd_register_uri_handler(s_http_server, &root_uri);

        httpd_uri_t save_uri = {
            .uri      = "/save",
            .method   = HTTP_POST, // Было HTTPD_POST -> стало HTTP_POST
            .handler  = save_post_handler
        };
        httpd_register_uri_handler(s_http_server, &save_uri);

        // Captive portal перенаправления для iOS / Android
        httpd_uri_t redirect_uri = {
            .uri      = "/*",
            .method   = HTTP_GET,  // Было HTTPD_GET -> стало HTTP_GET
            .handler  = root_get_handler
        };
        httpd_register_uri_handler(s_http_server, &redirect_uri);
    }
}

// --- WI-FI ОБРАБОТЧИК СОБЫТИЙ ---

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying connection to AP...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void start_soft_ap(void) {
    s_current_mode = WIFI_APP_MODE_AP;
    esp_wifi_stop();

    esp_netif_create_default_wifi_ap();

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

    ESP_LOGI(TAG, "SoftAP started. SSID: %s, Pass: %s", AP_SSID, AP_PASS);

    start_web_server();
}

static bool connect_to_saved_wifi(const char *ssid, const char *pass) {
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        s_current_mode = WIFI_APP_MODE_STA;
        ESP_LOGI(TAG, "Successfully connected to Wi-Fi!");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi.");
        return false;
    }
}

void wifi_app_init(void) {
    // 1. Инициализация NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Инициализация сетевого стека
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL, NULL));

    // 3. Попытка прочитать сохраненные данные из NVS
    char ssid[32] = {0};
    char pass[64] = {0};

    if (wifi_app_read_credentials(ssid, pass) == ESP_OK && strlen(ssid) > 0) {
        if (!connect_to_saved_wifi(ssid, pass)) {
            // Не смогли подключиться к роутеру (пароль изменился или выключен) -> Включаем SoftAP
            start_soft_ap();
        }
    } else {
        // Настройки отсутствуют -> Переходим в режим конфигурирования
        ESP_LOGW(TAG, "No Wi-Fi credentials found in NVS. Starting SoftAP...");
        start_soft_ap();
    }
}

wifi_app_mode_t wifi_app_get_mode(void) {
    return s_current_mode;
}
