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

#define AP_SSID            "ESP32_Config_1"
#define AP_PASS            "12345678"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static wifi_app_mode_t s_current_mode = WIFI_APP_MODE_NONE;
static httpd_handle_t s_http_server = NULL;
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_sta = NULL;
static TaskHandle_t s_dns_task_handle = NULL;
static int s_retry_num = 0;

static const char *HTML_FORM = 
    "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>body{font-family:Arial;background:#1a1a1a;color:#fff;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
    ".card{background:#2a2a2a;padding:25px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,0.5);width:85%;max-width:350px;}"
    "h2{margin-top:0;color:#00d2ff;text-align:center;}input{width:100%;padding:10px;margin:8px 0 16px 0;border:none;border-radius:6px;box-sizing:border-box;}"
    "button{width:100%;background:#00d2ff;color:#000;font-weight:bold;padding:12px;border:none;border-radius:6px;cursor:pointer;}"
    "</style></head><body><div class=\"card\"><h2>Terminal Wi-Fi</h2>"
    "<form action=\"/save\" method=\"POST\">"
    "<label>SSID (Название Wi-Fi):</label><input type=\"text\" name=\"ssid\" required>"
    "<label>Пароль:</label><input type=\"password\" name=\"pass\">"
    "<button type=\"submit\">Сохранить и перезагрузить</button>"
    "</form></div></body></html>";

// --- ФУНКЦИЯ ДЕКОДИРОВАНИЯ URL (Спецсимволы и пробелы) ---
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a' && a <= 'f') a -= 'a' - 'A';
            if (a >= 'A' && a <= 'F') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a' && b <= 'f') b -= 'a' - 'A';
            if (b >= 'A' && b <= 'F') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// --- DNS SERVER (CAPTIVE PORTAL TASK) ---
static void dns_server_task(void *pvParameters) {
    uint8_t rx_buffer[128];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Captive Portal started on port 53");

    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (len > 12) {
            // Формируем DNS-ответ: меняем флаги на Response (0x8400)
            rx_buffer[2] = 0x84;
            rx_buffer[3] = 0x00;
            rx_buffer[6] = 0x00; rx_buffer[7] = 0x01; // Answer count = 1

            // Ответ: IP адрес AP (192.168.4.1)
            uint8_t answer[] = {
                0xc0, 0x0c,             // Pointer to domain name
                0x00, 0x01,             // Type A
                0x00, 0x01,             // Class IN
                0x00, 0x00, 0x00, 0x3c, // TTL 60 sec
                0x00, 0x04,             // Data length 4
                192, 168, 4, 1          // IP Address 192.168.4.1
            };

            if (len + sizeof(answer) <= sizeof(rx_buffer)) {
                memcpy(rx_buffer + len, answer, sizeof(answer));
                sendto(sock, rx_buffer, len + sizeof(answer), 0, (struct sockaddr *)&client_addr, client_addr_len);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// --- NVS ---
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

// --- HTTP HANDLERS ---
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Редирект для Captive Portal (302 Redirect)
static esp_err_t redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    char raw_ssid[64] = {0}, raw_pass[64] = {0};
    char ssid[32] = {0}, pass[64] = {0};

    char *ssid_ptr = strstr(buf, "ssid=");
    char *pass_ptr = strstr(buf, "pass=");

    if (ssid_ptr) sscanf(ssid_ptr, "ssid=%63[^&]", raw_ssid);
    if (pass_ptr) sscanf(pass_ptr, "pass=%63s", raw_pass);

    url_decode(ssid, raw_ssid);
    url_decode(pass, raw_pass);

    ESP_LOGI(TAG, "Received Credentials: SSID='%s'", ssid);
    wifi_app_save_credentials(ssid, pass);

    const char *resp = "<html><body style=\"background:#1a1a1a;color:#fff;text-align:center;\">"
                       "<h2>Saved! Restarting...</h2></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

static void start_web_server(void) {
    if (s_http_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(s_http_server, &root_uri);

        httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
        httpd_register_uri_handler(s_http_server, &save_uri);

        // Служебные URL для iOS / Android (отдаем главную страницу)
        httpd_uri_t captive_uris[] = {
            { .uri = "/generate_204", .method = HTTP_GET, .handler = redirect_handler },
            { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect_handler },
            { .uri = "/canonical.html", .method = HTTP_GET, .handler = redirect_handler }
        };

        for (int i = 0; i < 3; i++) {
            httpd_register_uri_handler(s_http_server, &captive_uris[i]);
        }
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
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

    // Запуск HTTP и DNS Серверов
    start_web_server();
    if (s_dns_task_handle == NULL) {
        xTaskCreate(dns_server_task, "dns_task", 3072, NULL, 5, &s_dns_task_handle);
    }
}

static bool connect_to_saved_wifi(const char *ssid, const char *pass) {
    s_wifi_event_group = xEventGroupCreate();

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
            pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

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
        if (!connect_to_saved_wifi(ssid, pass)) {
            start_soft_ap();
        }
    } else {
        start_soft_ap();
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
