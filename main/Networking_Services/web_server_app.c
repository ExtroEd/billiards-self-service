#include "web_server_app.h"
#include "wifi_app.h"
#include "system_state.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t s_http_server = NULL;
static TaskHandle_t s_dns_task_handle = NULL;

// Встроенный HTML из секции binary
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
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

// DNS Captive Portal Task
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
            rx_buffer[2] = 0x84;
            rx_buffer[3] = 0x00;
            rx_buffer[6] = 0x00; rx_buffer[7] = 0x01;

            uint8_t answer[] = {
                0xc0, 0x0c,
                0x00, 0x01,
                0x00, 0x01,
                0x00, 0x00, 0x00, 0x3c,
                0x00, 0x04,
                192, 168, 4, 1
            };

            if (len + sizeof(answer) <= sizeof(rx_buffer)) {
                memcpy(rx_buffer + len, answer, sizeof(answer));
                sendto(sock, rx_buffer, len + sizeof(answer), 0, (struct sockaddr *)&client_addr, client_addr_len);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// HTTP Route Handlers
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    const size_t index_html_len = index_html_end - index_html_start;
    httpd_resp_send(req, (const char *)index_html_start, index_html_len);
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_close_post_handler(httpd_req_t *req) {
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Получен запрос закрытия сессии от клиента. Возврат к Wi-Fi...");
    
    vTaskDelay(pdMS_TO_TICKS(200));
    wifi_app_stop_ap_and_reconnect();
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
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

    const char *resp = "<!DOCTYPE html><html><body style=\"background:#1a1a1a;color:#fff;text-align:center;\">"
                       "<h2>Сохранено! Перезагрузка...</h2></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_status_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");

    char json_resp[180];
    snprintf(json_resp, sizeof(json_resp),
             "{\"balance\":%d,\"total_money\":%ld,\"min_threshold\":%d,\"price_per_1hour\":%d,\"relay\":%s}",
             system_state_get_balance(),
             (long)system_state_get_total_money(),
             system_state_get_min_threshold(),
             system_state_get_price_per_1hour(),
             system_state_is_relay_active() ? "true" : "false");

    httpd_resp_send(req, json_resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_settings_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    int min_th = -1, price = -1;
    char *min_ptr = strstr(buf, "min_threshold=");
    char *price_ptr = strstr(buf, "price_per_1hour=");

    if (min_ptr) sscanf(min_ptr, "min_threshold=%d", &min_th);
    if (price_ptr) sscanf(price_ptr, "price_per_1hour=%d", &price);

    if (min_th >= 5 && min_th <= 1000) {
        system_state_set_min_threshold(min_th);
    }
    if (price >= 5 && price <= 5000) {
        system_state_set_price_per_1hour(price);
    }

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_action_post_handler(httpd_req_t *req) {
    char buf[64] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    if (strstr(buf, "action=add10")) {
        system_state_add_credit(10);
    } else if (strstr(buf, "action=reset_time")) {
        system_state_reset_balance();
    } else if (strstr(buf, "action=reset_total")) {
        system_state_reset_total_money();
    }

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void web_server_app_start(void) {
    if (s_http_server != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 15;
    config.lru_purge_enable = true;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(s_http_server, &root_uri);

        httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
        httpd_register_uri_handler(s_http_server, &save_uri);

        httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_get_handler };
        httpd_register_uri_handler(s_http_server, &status_uri);

        httpd_uri_t settings_uri = { .uri = "/api/settings", .method = HTTP_POST, .handler = api_settings_post_handler };
        httpd_register_uri_handler(s_http_server, &settings_uri);

        httpd_uri_t action_uri = { .uri = "/api/action", .method = HTTP_POST, .handler = api_action_post_handler };
        httpd_register_uri_handler(s_http_server, &action_uri);

        httpd_uri_t close_uri = { .uri = "/api/close", .method = HTTP_POST, .handler = api_close_post_handler };
        httpd_register_uri_handler(s_http_server, &close_uri);

        httpd_uri_t favicon_uri = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler };
        httpd_register_uri_handler(s_http_server, &favicon_uri);

        const char *captive_paths[] = {
            "/generate_204", "/gen_204", "/connecttest.txt",
            "/redirect", "/hotspot-detect.html", "/canonical.html", "/library/test/success.html"
        };

        for (size_t i = 0; i < sizeof(captive_paths) / sizeof(captive_paths[0]); i++) {
            httpd_uri_t captive_uri = {
                .uri      = captive_paths[i],
                .method   = HTTP_GET,
                .handler  = redirect_handler,
                .user_ctx = NULL
            };
            httpd_register_uri_handler(s_http_server, &captive_uri);
        }
    }

    if (s_dns_task_handle == NULL) {
        xTaskCreate(dns_server_task, "dns_task", 3072, NULL, 5, &s_dns_task_handle);
    }
}

void web_server_app_stop(void) {
    if (s_dns_task_handle != NULL) {
        vTaskDelete(s_dns_task_handle);
        s_dns_task_handle = NULL;
    }
    if (s_http_server != NULL) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
    }
}
