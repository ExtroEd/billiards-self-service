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

static const char *TAG = "DISPLAY_TFT";
static spi_device_handle_t s_spi_dev = NULL;

static bool s_is_awake = true;
static int64_t s_last_activity_time = 0;
#define SLEEP_TIMEOUT_US (30 * 1000 * 1000LL) // 30 секунд

#define ST7735_OFFSET_X  2
#define ST7735_OFFSET_Y  1

typedef enum {
    UI_STATE_MAIN_SCREEN,
    UI_STATE_MENU_NAV,   
    UI_STATE_MENU_EDIT,
    UI_STATE_SHOW_QR
} ui_state_t;

static ui_state_t s_ui_state = UI_STATE_MAIN_SCREEN;
static int8_t s_selected_item = 0;

static void render_menu(void);
static void qrcode_display_cb(esp_qrcode_handle_t qrcode);

#define MENU_ITEMS_COUNT 7
static const char *MENU_LABELS[MENU_ITEMS_COUNT] = {
    "1.Мин.старт",
    "2.Цена 10мин",
    "3.Касса всего",
    "4.Сброс кассы",
    "5.Сброс время",
    "6.Сохранить",
    "7.Вай-Фай"
};

static device_config_t s_config = {
    .min_start_sum = 30,
    .price_per_10min = 30,
    .total_money = 0,
    .current_balance = 0,
    .remaining_time_s = 0
};

// Шрифт 5x7: ASCII (0..32) + Кириллица А-Я (33..65)
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space (0)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !     (1)
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "     (2)
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #     (3)
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $     (4)
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %     (5)
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &     (6)
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '     (7)
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (     (8)
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )     (9)
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *     (10)
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +     (11)
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,     (12)
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -     (13)
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .     (14)
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /     (15)
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0     (16)
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1     (17)
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2     (18)
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3     (19)
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4     (20)
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5     (21)
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6     (22)
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7     (23)
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8     (24)
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9     (25)
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :     (26)
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;     (27)
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <     (28)
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =     (29)
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >     (30)
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?     (31)
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @     (32)
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, // А     (33)
    {0x7F, 0x49, 0x49, 0x49, 0x31}, // Б     (34)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // В     (35)
    {0x7F, 0x01, 0x01, 0x01, 0x03}, // Г     (36)
    {0xE0, 0x1F, 0x11, 0x1F, 0xE0}, // Д     (37)
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // Е     (38)
    {0x7D, 0x48, 0x48, 0x48, 0x41}, // Ё     (39)
    {0x77, 0x08, 0x7F, 0x08, 0x77}, // Ж     (40)
    {0x41, 0x49, 0x49, 0x49, 0x36}, // З     (41)
    {0x7F, 0x10, 0x08, 0x04, 0x7F}, // И     (42)
    {0x7F, 0x10, 0x09, 0x04, 0x7F}, // Й     (43)
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // К     (44)
    {0x40, 0x3F, 0x01, 0x01, 0x7F}, // Л     (45)
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // М     (46)
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // Н     (47)
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // О     (48)
    {0x7F, 0x01, 0x01, 0x01, 0x7F}, // П     (49)
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // Р     (50)
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // С     (51)
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // Т     (52)
    {0x0F, 0x50, 0x50, 0x50, 0x3F}, // У     (53)
    {0x1C, 0x22, 0x7F, 0x22, 0x1C}, // Ф     (54)
    {0x63, 0x14, 0x08, 0x14, 0x63}, // Х     (55)
    {0x7F, 0x40, 0x40, 0x7F, 0xC0}, // Ц     (56)
    {0x07, 0x08, 0x08, 0x08, 0x7F}, // Ч     (57)
    {0x7F, 0x40, 0x7F, 0x40, 0x7F}, // Ш     (58)
    {0x7F, 0x40, 0x7F, 0x40, 0xFF}, // Щ     (59)
    {0x01, 0x7F, 0x48, 0x48, 0x30}, // Ъ     (60)
    {0x7F, 0x48, 0x30, 0x00, 0x7F}, // Ы     (61)
    {0x00, 0x7F, 0x48, 0x48, 0x30}, // Ь     (62)
    {0x22, 0x41, 0x49, 0x49, 0x3E}, // Э     (63)
    {0x7F, 0x08, 0x3E, 0x41, 0x3E}, // Ю     (64)
    {0x46, 0x29, 0x19, 0x09, 0x7F}  // Я     (65)
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
        if (s_ui_state != UI_STATE_MAIN_SCREEN) {
            // Перерисуем экран при пробуждении
            render_menu();
        }
    }
}

void display_tft_sleep(void) {
    display_tft_set_backlight(false);
    s_ui_state = UI_STATE_MAIN_SCREEN;
}

// Функция регулярной проверки активности (вызывать в основном цикле app_main)
void display_tft_tick(void) {
    if (s_is_awake) {
        if (esp_timer_get_time() - s_last_activity_time > SLEEP_TIMEOUT_US) {
            display_tft_sleep();
        }
    }
}

// Попиксельный перевод символа в массив байтов RGB565 для ST7735
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

// Вывод строки с парсингом UTF-8
void display_tft_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    const uint8_t *p = (const uint8_t *)str;

    while (*p) {
        if (x + 6 > DISPLAY_WIDTH) break;

        uint8_t idx = 31; // '?' по умолчанию

        // 1. ASCII Символы (1 байт)
        if (*p < 0x80) {
            uint8_t c = *p;
            if (c >= ' ' && c <= '@') {
                idx = c - ' ';
            } else if (c >= '0' && c <= '9') {
                idx = 16 + (c - '0');
            } else if (c >= 'A' && c <= 'Z') {
                idx = c - ' ';
            } else if (c >= 'a' && c <= 'z') {
                // Приводим строчные английские к заглавным
                idx = (c - 32) - ' ';
            } else {
                idx = 0;
            }
            p++; // Сдвиг на 1 байт
        } 
        // 2. UTF-8 Кириллица (2 байта)
        else if (*p == 0xD0 || *p == 0xD1) {
            if (*(p + 1) == '\0') break; // Страховка от вылета за край строки

            uint8_t b1 = *p++;
            uint8_t b2 = *p++;
            uint16_t unicode = ((b1 & 0x1F) << 6) | (b2 & 0x3F);

            // Обработка Ё / ё
            if (unicode == 0x0401 || unicode == 0x0451) {
                idx = 39; // Ё в font5x7
            }
            // Заглавные А-Я (0x0410 .. 0x042F)
            else if (unicode >= 0x0410 && unicode <= 0x042F) {
                uint8_t offset = unicode - 0x0410;
                // Учитываем, что в font5x7 буквой 39 является Ё,
                // поэтому буквы после Е (начиная с Ж) сдвинуты на +1
                if (offset >= 6) { // Начиная с Ж (offset 6)
                    idx = 33 + offset + 1;
                } else { // А, Б, В, Г, Д, Е
                    idx = 33 + offset;
                }
            } 
            // Строчные а-я (0x0430 .. 0x044F) -> приводим к заглавным
            else if (unicode >= 0x0430 && unicode <= 0x044F) {
                uint8_t offset = unicode - 0x0430;
                if (offset >= 6) {
                    idx = 33 + offset + 1;
                } else {
                    idx = 33 + offset;
                }
            }
        } 
        // Неизвестный байт
        else {
            p++;
        }

        draw_glyph_fast(x, y, idx, color, bg_color);
        x += 6;
    }
}

static void render_menu(void) {
    static bool first_render = true;

    if (first_render) {
        display_tft_fill_screen(COLOR_BLACK);
        display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
        display_tft_draw_string(10, 4, "Настройки", COLOR_WHITE, COLOR_BLUE);
        first_render = false;
    }

    // Шаг по Y: (160 - 16 шапка) / 7 пунктов = ~20px на элемент. 
    // Для компактности берем высоту элемента 16px и отступ 2px
    const int item_height = 16;
    const int start_y = 18;

    for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
        uint16_t text_color = COLOR_WHITE;
        uint16_t bg_color = COLOR_BLACK;

        if (i == s_selected_item) {
            bg_color = (s_ui_state == UI_STATE_MENU_EDIT) ? COLOR_RED : COLOR_DARKGRAY;
        }

        int cur_y = start_y + (i * (item_height + 2));

        display_tft_fill_rect(2, cur_y, 124, item_height, bg_color);

        char buf[24];
        if (i == 0) snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[i], (long)s_config.min_start_sum);
        else if (i == 1) snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[i], (long)s_config.price_per_10min);
        else if (i == 2) snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[i], (long)s_config.total_money);
        else snprintf(buf, sizeof(buf), "%s", MENU_LABELS[i]);

        display_tft_draw_string(4, cur_y + 4, buf, text_color, bg_color);
    }
}

void menu_process_event(encoder_event_t event) {
    display_tft_wake();

    if (s_ui_state == UI_STATE_MAIN_SCREEN) {
        if (event == ENCODER_EVENT_CLICK || event == ENCODER_EVENT_LONG_PRESS) {
            s_ui_state = UI_STATE_MENU_NAV;
            s_selected_item = 0;
            render_menu();
        }
        return;
    }

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
            if (s_selected_item == 0 || s_selected_item == 1) {
                s_ui_state = UI_STATE_MENU_EDIT;
            } else if (s_selected_item == 3) {
                s_config.total_money = 0;
                ESP_LOGI(TAG, "Money reset!");
            } else if (s_selected_item == 4) {
                s_config.remaining_time_s = 0;
                s_config.current_balance = 0;
                ESP_LOGI(TAG, "Time reset!");
            } else if (s_selected_item == 5) {
                // Выход из меню
                display_tft_sleep();
                return;
            } else if (s_selected_item == 6) {
                ESP_LOGI(TAG, "Открытие QR-кода для настройки Wi-Fi...");

                // 1. Включаем SoftAP (правильная функция из wifi_app.h)
                wifi_app_start_ap_mode(); 

                // 2. Формируем строку Wi-Fi
                const char *qr_payload = "WIFI:S:ESP32_Config;T:WPA;P:12345678;;";

                // 3. Конфигурируем и генерируем QR-код
                esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
                cfg.display_func = qrcode_display_cb; // Передаем нашу callback-функцию
                cfg.max_qrcode_version = 10;

                // Функция генерирует QR и сама передает handle в qrcode_display_cb
                esp_err_t err = esp_qrcode_generate(&cfg, qr_payload);
                if (err == ESP_OK) {
                    s_ui_state = UI_STATE_SHOW_QR;
                } else {
                    ESP_LOGE(TAG, "Ошибка генерации QR-кода: %d", err);
                }
                return;
            }
            render_menu();
        }
    } 
    else if (s_ui_state == UI_STATE_MENU_EDIT) {
        if (s_selected_item == 0) {
            if (event == ENCODER_EVENT_UP && s_config.min_start_sum < 1000) s_config.min_start_sum += 5;
            if (event == ENCODER_EVENT_DOWN && s_config.min_start_sum > 10) s_config.min_start_sum -= 5;
        } else if (s_selected_item == 1) {
            if (event == ENCODER_EVENT_UP && s_config.price_per_10min < 500) s_config.price_per_10min += 5;
            if (event == ENCODER_EVENT_DOWN && s_config.price_per_10min > 5) s_config.price_per_10min -= 5;
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
    ESP_LOGI(TAG, "ST7735 initialized successfully.");
}

/**
 * @brief Отрисовка QR-кода на ST7735 с автоматическим центрированием
 */
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

// Callback-функция, которую вызывает сама библиотека esp_qrcode
static void qrcode_display_cb(esp_qrcode_handle_t qrcode) {
    display_tft_draw_qrcode(qrcode, 3); // Рисуем с масштабом 3
}
