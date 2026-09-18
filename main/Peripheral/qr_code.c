#include <stdio.h>
#include "qrcode.h"       // Подключается из managed_components/espressif__qrcode
#include "display_tft.h"  // Ваши функции отрисовки
#include "qr_code.h"

// Callback-функция, которую библиотека вызовет для вывода QR-кода на экран
static void draw_qr_code_cb(esp_qrcode_handle_t qrcode) {
    int size = esp_qrcode_get_size(qrcode);
    int scale = 3; // Масштаб пикселей для ST7735 (128x160)
    int offset_x = (128 - (size * scale)) / 2;
    int offset_y = 22;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            // Если модуль включен — черный, иначе — белый
            uint16_t color = esp_qrcode_get_module(qrcode, x, y) ? COLOR_BLACK : COLOR_WHITE;
            display_tft_fill_rect(offset_x + x * scale, offset_y + y * scale, scale, scale, color);
        }
    }
}

void display_show_wifi_qr(const char *ssid, const char *password) {
    char qr_data[128];
    snprintf(qr_data, sizeof(qr_data), "WIFI:S:%s;T:WPA;P:%s;;", ssid, password);

    // Подготовка экрана
    display_tft_fill_screen(COLOR_WHITE);
    display_tft_draw_string(10, 5, "Сканируй Wi-Fi", COLOR_BLACK, COLOR_WHITE);

    // Конфигурация генератора QR-кода Espressif
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = draw_qr_code_cb;
    cfg.max_qrcode_version = 3; // Максимальная версия QR-кода

    // Генерация и автоматический вызов draw_qr_code_cb
    esp_qrcode_generate(&cfg, qr_data);

    display_tft_draw_string(5, 120, "IP: 192.168.4.1", COLOR_BLACK, COLOR_WHITE);
    display_tft_draw_string(5, 138, "Нажмите для вых.", COLOR_DARKGRAY, COLOR_WHITE);
}
