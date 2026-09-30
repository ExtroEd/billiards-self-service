#include "display_tft.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "Networking_Services/wifi_app.h"
#include "qr_code.h"
#include "qrcode.h"
#include "system_state.h"

static const char *TAG = "DISPLAY_TFT";
static spi_device_handle_t s_spi_dev = NULL;

static bool s_is_awake = true;
static int64_t s_last_activity_time = 0;
#define SLEEP_TIMEOUT_US (60 * 1000 * 1000LL) // 60 секунд

#define ST7735_OFFSET_X  2
#define ST7735_OFFSET_Y  1

typedef enum {
    UI_STATE_MENU_NAV,   
    UI_STATE_MENU_EDIT,
    UI_STATE_SHOW_QR
} ui_state_t;

// Начальное состояние теперь сразу Меню
static ui_state_t s_ui_state = UI_STATE_MENU_NAV;
static int8_t s_selected_item = 0;

static void render_menu(void);
static void qrcode_display_cb(esp_qrcode_handle_t qrcode);

#define MENU_ITEMS_COUNT 7
#define VISIBLE_MENU_ITEMS 6 

static const char *MENU_LABELS[MENU_ITEMS_COUNT] = {
    "1.Вай-Фай",
    "2.Мин.старт",
    "3.Цена 1час",
    "4.Касса всего",
    "5.Сбр. кассы",
    "6.Сбр. времени",
    "7.+10 сом"
};

// Шрифт 5x7: ASCII (0..32) + Кириллица А-Я (33..65)
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7C, 0x12, 0x11, 0x12, 0x7C},
    {0x7F, 0x49, 0x49, 0x49, 0x31}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x7F, 0x01, 0x01, 0x01, 0x03}, {0xE0, 0x1F, 0x11, 0x1F, 0xE0},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7D, 0x48, 0x48, 0x48, 0x41},
    {0x77, 0x08, 0x7F, 0x08, 0x77}, {0x41, 0x49, 0x49, 0x49, 0x36},
    {0x7F, 0x10, 0x08, 0x04, 0x7F}, {0x7F, 0x10, 0x09, 0x04, 0x7F},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x40, 0x3F, 0x01, 0x01, 0x7F},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x01, 0x01, 0x01, 0x7F},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x0F, 0x50, 0x50, 0x50, 0x3F},
    {0x1C, 0x22, 0x7F, 0x22, 0x1C}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x7F, 0x40, 0x40, 0x7F, 0xC0}, {0x07, 0x08, 0x08, 0x08, 0x7F},
    {0x7F, 0x40, 0x7F, 0x40, 0x7F}, {0x7F, 0x40, 0x7F, 0x40, 0xFF},
    {0x01, 0x7F, 0x48, 0x48, 0x30}, {0x7F, 0x48, 0x30, 0x00, 0x7F},
    {0x00, 0x7F, 0x48, 0x48, 0x30}, {0x22, 0x41, 0x49, 0x49, 0x3E},
    {0x7F, 0x08, 0x3E, 0x41, 0x3E}, {0x46, 0x29, 0x19, 0x09, 0x7F}
};

static void display_tft_send_cmd(uint8_t cmd) {
    gpio_set_level(DISPLAY_DC_PIN, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_polling_transmit(s_spi_dev, &t);
}

static void display_tft_send_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(DISPLAY_DC_PIN, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    spi_device_polling_transmit(s_spi_dev, &t);
}

static void display_tft_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    x0 += ST7735_OFFSET_X;
    x1 += ST7735_OFFSET_X;
    y0 += ST7735_OFFSET_Y;
    y1 += ST7735_OFFSET_Y;

    display_tft_send_cmd(0x2A);
    uint8_t data_x[] = { 0x00, x0, 0x00, x1 };
    display_tft_send_data(data_x, 4);

    display_tft_send_cmd(0x2B);
    uint8_t data_y[] = { 0x00, y0, 0x00, y1 };
    display_tft_send_data(data_y, 4);

    display_tft_send_cmd(0x2C);
}

void display_tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;

    display_tft_set_window(x, y, x + w - 1, y + h - 1);

    size_t pixels = w * h;
    #define BUF_SIZE 512
    uint8_t line_buf[BUF_SIZE];
    uint8_t high_byte = color >> 8;
    uint8_t low_byte = color & 0xFF;

    for (int i = 0; i < BUF_SIZE; i += 2) {
        line_buf[i] = high_byte;
        line_buf[i + 1] = low_byte;
    }

    gpio_set_level(DISPLAY_DC_PIN, 1);
    while (pixels > 0) {
        size_t send_pixels = (pixels > BUF_SIZE / 2) ? BUF_SIZE / 2 : pixels;
        spi_transaction_t t = {
            .length = send_pixels * 16,
            .tx_buffer = line_buf
        };
        spi_device_polling_transmit(s_spi_dev, &t);
        pixels -= send_pixels;
    }
}

void display_tft_fill_screen(uint16_t color) {
    display_tft_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

void display_tft_set_backlight(bool enable) {
    gpio_set_level(DISPLAY_BL_PIN, enable ? 1 : 0);
    s_is_awake = enable;
}

void display_tft_wake(void) {
    s_last_activity_time = esp_timer_get_time();
    if (!s_is_awake) {
        display_tft_set_backlight(true);
        if (s_ui_state != UI_STATE_SHOW_QR) {
            render_menu();
        }
    }
}

void display_tft_sleep(void) {
    display_tft_set_backlight(false);
}

void display_tft_tick(void) {
    if (s_is_awake) {
        if (esp_timer_get_time() - s_last_activity_time > SLEEP_TIMEOUT_US) {
            display_tft_sleep();
        }
    }
}

static void draw_glyph_fast(int16_t x, int16_t y, uint8_t idx, uint16_t color, uint16_t bg_color) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT || x + 5 > DISPLAY_WIDTH || y + 7 > DISPLAY_HEIGHT) return;

    display_tft_set_window(x, y, x + 4, y + 6);

    uint8_t glyph_buf[35 * 2];
    uint16_t pos = 0;

    uint8_t c_hi = color >> 8, c_lo = color & 0xFF;
    uint8_t bg_hi = bg_color >> 8, bg_lo = bg_color & 0xFF;

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            uint8_t bit = (font5x7[idx][col] >> row) & 0x01;
            glyph_buf[pos++] = bit ? c_hi : bg_hi;
            glyph_buf[pos++] = bit ? c_lo : bg_lo;
        }
    }

    gpio_set_level(DISPLAY_DC_PIN, 1);
    spi_transaction_t t = {
        .length = sizeof(glyph_buf) * 8,
        .tx_buffer = glyph_buf
    };
    spi_device_polling_transmit(s_spi_dev, &t);
}

void display_tft_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    const uint8_t *p = (const uint8_t *)str;

    while (*p) {
        if (x + 5 > DISPLAY_WIDTH) break;

        uint8_t idx = 31; 

        if (*p < 0x80) {
            uint8_t c = *p;
            if (c >= ' ' && c <= '@') {
                idx = c - ' ';
            } else if (c >= '0' && c <= '9') {
                idx = 16 + (c - '0');
            } else if (c >= 'A' && c <= 'Z') {
                idx = c - ' ';
            } else if (c >= 'a' && c <= 'z') {
                idx = (c - 32) - ' ';
            } else {
                idx = 0;
            }
            p++;
        } 
        else if (*p == 0xD0 || *p == 0xD1) {
            if (*(p + 1) == '\0') break;

            uint8_t b1 = *p++;
            uint8_t b2 = *p++;
            uint16_t unicode = ((b1 & 0x1F) << 6) | (b2 & 0x3F);

            if (unicode == 0x0401 || unicode == 0x0451) {
                idx = 39;
            } else if (unicode >= 0x0410 && unicode <= 0x042F) {
                uint8_t offset = unicode - 0x0410;
                idx = (offset >= 6) ? (33 + offset + 1) : (33 + offset);
            } else if (unicode >= 0x0430 && unicode <= 0x044F) {
                uint8_t offset = unicode - 0x0430;
                idx = (offset >= 6) ? (33 + offset + 1) : (33 + offset);
            }
        } else {
            p++;
        }

        draw_glyph_fast(x, y, idx, color, bg_color);
        x += 6;
    }
}

static void render_menu(void) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
    display_tft_draw_string(10, 4, "Настройки", COLOR_WHITE, COLOR_BLUE);

    const int item_height = 18;
    const int start_y = 20;

    int top_index = 0;
    if (s_selected_item >= VISIBLE_MENU_ITEMS) {
        top_index = s_selected_item - VISIBLE_MENU_ITEMS + 1;
    }

    for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
        int item_idx = top_index + i;
        if (item_idx >= MENU_ITEMS_COUNT) break;

        uint16_t text_color = COLOR_WHITE;
        uint16_t bg_color = COLOR_BLACK;

        if (item_idx == s_selected_item) {
            bg_color = (s_ui_state == UI_STATE_MENU_EDIT) ? COLOR_RED : COLOR_DARKGRAY;
        }

        int cur_y = start_y + (i * (item_height + 2));

        display_tft_fill_rect(2, cur_y, 124, item_height, bg_color);

        char buf[24];
        if (item_idx == 0) {
            snprintf(buf, sizeof(buf), "%s", MENU_LABELS[item_idx]);
        } else if (item_idx == 1) {
            snprintf(buf, sizeof(buf), "%s:%d", MENU_LABELS[item_idx], system_state_get_min_threshold());
        } else if (item_idx == 2) {
            snprintf(buf, sizeof(buf), "%s:%d", MENU_LABELS[item_idx], system_state_get_price_per_1hour());
        } else if (item_idx == 3) {
            snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[item_idx], (long)system_state_get_total_money());
        } else {
            snprintf(buf, sizeof(buf), "%s", MENU_LABELS[item_idx]);
        }

        display_tft_draw_string(4, cur_y + 4, buf, text_color, bg_color);
    }
}

void menu_process_event(encoder_event_t event) {
    display_tft_wake();

    if (s_ui_state == UI_STATE_SHOW_QR) {
        if (event == ENCODER_EVENT_CLICK || event == ENCODER_EVENT_LONG_PRESS) {
            s_ui_state = UI_STATE_MENU_NAV;
            render_menu();
        }
        return;
    }

    if (s_ui_state == UI_STATE_MENU_NAV) {
        if (event == ENCODER_EVENT_UP) {
            s_selected_item = (s_selected_item > 0) ? s_selected_item - 1 : MENU_ITEMS_COUNT - 1;
            render_menu();
        } else if (event == ENCODER_EVENT_DOWN) {
            s_selected_item = (s_selected_item < MENU_ITEMS_COUNT - 1) ? s_selected_item + 1 : 0;
            render_menu();
        } else if (event == ENCODER_EVENT_CLICK) {
            if (s_selected_item == 0) {
                // 1. Вай-Фай
                ESP_LOGI(TAG, "Открытие QR-кода для настройки Wi-Fi...");
                wifi_app_start_ap_mode(); 

                const char *qr_payload = "WIFI:S:ESP32_Config;T:WPA;P:12345678;;";
                esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
                cfg.display_func = qrcode_display_cb;
                cfg.max_qrcode_version = 10;

                esp_err_t err = esp_qrcode_generate(&cfg, qr_payload);
                if (err == ESP_OK) {
                    s_ui_state = UI_STATE_SHOW_QR;
                } else {
                    ESP_LOGE(TAG, "Ошибка генерации QR-кода: %d", err);
                }
                return;
            } else if (s_selected_item == 1 || s_selected_item == 2) {
                // Мин. старт или Цена за 1 час
                s_ui_state = UI_STATE_MENU_EDIT;
            } else if (s_selected_item == 4) {
                // Сброс кассы
                system_state_reset_total_money();
            } else if (s_selected_item == 5) {
                // Сброс времени
                system_state_reset_balance();
            } else if (s_selected_item == 6) {
                // +10 сом
                system_state_add_credit(10);
            }
            render_menu();
        }
    } 
    else if (s_ui_state == UI_STATE_MENU_EDIT) {
        if (s_selected_item == 1) {
            int th = system_state_get_min_threshold();
            if (event == ENCODER_EVENT_UP && th < 1000) system_state_set_min_threshold(th + 5);
            if (event == ENCODER_EVENT_DOWN && th >= 5) system_state_set_min_threshold(th - 5);
        } else if (s_selected_item == 2) {
            int price = system_state_get_price_per_1hour();
            if (event == ENCODER_EVENT_UP && price < 5000) system_state_set_price_per_1hour(price + 5);
            if (event == ENCODER_EVENT_DOWN && price > 5) system_state_set_price_per_1hour(price - 5);
        }

        if (event == ENCODER_EVENT_CLICK) {
            s_ui_state = UI_STATE_MENU_NAV;
        }
        render_menu();
    }
}

void display_tft_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_DC_PIN) | (1ULL << DISPLAY_RES_PIN) | (1ULL << DISPLAY_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    spi_bus_config_t buscfg = {
        .mosi_io_num = DISPLAY_SDA_PIN,
        .sclk_io_num = DISPLAY_SCL_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = DISPLAY_CS_PIN,
        .queue_size = 7
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_dev));

    gpio_set_level(DISPLAY_RES_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(DISPLAY_RES_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    display_tft_send_cmd(0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
    display_tft_send_cmd(0x11); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(200));
    display_tft_send_cmd(0x3A); // COLMOD (16-bit)
    uint8_t colmod = 0x05;
    display_tft_send_data(&colmod, 1);
    display_tft_send_cmd(0x29); // DISPON

    display_tft_set_backlight(true);
    s_last_activity_time = esp_timer_get_time();
    
    // Сразу отрисовываем меню
    render_menu();
    ESP_LOGI(TAG, "ST7735 initialized successfully with Menu screen.");
}

void display_tft_draw_qrcode(esp_qrcode_handle_t qrcode, uint8_t scale) {
    if (!qrcode) return;

    int qr_size = esp_qrcode_get_size(qrcode);
    uint16_t qr_size_px = qr_size * scale;
    
    if (qr_size_px > DISPLAY_WIDTH || qr_size_px > DISPLAY_HEIGHT) {
        ESP_LOGE(TAG, "QR-код слишком большой для экрана! (Size: %d px)", qr_size_px);
        return;
    }

    int16_t start_x = (DISPLAY_WIDTH - qr_size_px) / 2;
    int16_t start_y = (DISPLAY_HEIGHT - qr_size_px) / 2;

    display_tft_fill_screen(COLOR_WHITE);

    for (uint8_t y = 0; y < qr_size; y++) {
        for (uint8_t x = 0; x < qr_size; x++) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                display_tft_fill_rect(
                    start_x + (x * scale),
                    start_y + (y * scale),
                    scale,
                    scale,
                    COLOR_BLACK
                );
            }
        }
    }
}

static void qrcode_display_cb(esp_qrcode_handle_t qrcode) {
    display_tft_draw_qrcode(qrcode, 3);
}
