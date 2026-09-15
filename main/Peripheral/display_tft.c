#include "display_tft.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DISPLAY_TFT";
static spi_device_handle_t s_spi_dev = NULL;

// Состояние дисплея и таймаут автовыключения (15 секунд)
static bool s_is_awake = true;
static int64_t s_last_activity_time = 0;
#define SLEEP_TIMEOUT_US (15 * 1000 * 1000)

// Смещение для фиксации сдвига изображения ST7735 (128x160)
#define ST7735_OFFSET_X  2
#define ST7735_OFFSET_Y  1

// Состояния меню
typedef enum {
    UI_STATE_MAIN_SCREEN, // Главный экран
    UI_STATE_MENU_NAV,    // Навигация по меню
    UI_STATE_MENU_EDIT    // Редактирование параметра
} ui_state_t;

static ui_state_t s_ui_state = UI_STATE_MAIN_SCREEN;
static int8_t s_selected_item = 0;

#define MENU_ITEMS_COUNT 6
static const char *MENU_LABELS[MENU_ITEMS_COUNT] = {
    "1.Мин.старт",
    "2.Цена 10мин",
    "3.Касса всего",
    "4.Сброс кассы",
    "5.Сброс время",
    "6.Сохранить"
};

// Используем определение из display_tft.h
static device_config_t s_config = {
    .min_start_sum = 30,
    .price_per_10min = 30,
    .total_money = 0,
    .current_balance = 0,
    .remaining_time_s = 0
};

// Стандартный шрифт ASCII 5x7
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}  // Z
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

    display_tft_send_cmd(0x2A); // CASET
    uint8_t data_x[] = { 0x00, x0, 0x00, x1 };
    display_tft_send_data(data_x, 4);

    display_tft_send_cmd(0x2B); // RASET
    uint8_t data_y[] = { 0x00, y0, 0x00, y1 };
    display_tft_send_data(data_y, 4);

    display_tft_send_cmd(0x2C); // RAMWR
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
    }
}

void display_tft_sleep(void) {
    display_tft_set_backlight(false);
    s_ui_state = UI_STATE_MAIN_SCREEN;
}

void display_tft_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg_color) {
    if (c < ' ' || c > 'Z') c = '?';
    uint8_t idx = c - ' ';

    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[idx][i];
        for (int j = 0; j < 7; j++) {
            uint16_t pixel_color = (line & (1 << j)) ? color : bg_color;
            display_tft_fill_rect(x + i, y + j, 1, 1, pixel_color);
        }
    }
}

void display_tft_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    while (*str) {
        if (x + 6 > DISPLAY_WIDTH) break;
        display_tft_draw_char(x, y, *str, color, bg_color);
        x += 6;
        str++;
    }
}

// Отрисовка меню
static void render_menu(void) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
    display_tft_draw_string(10, 4, "SETTINGS", COLOR_WHITE, COLOR_BLUE);

    for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
        uint16_t text_color = COLOR_WHITE;
        uint16_t bg_color = COLOR_BLACK;

        if (i == s_selected_item) {
            bg_color = (s_ui_state == UI_STATE_MENU_EDIT) ? COLOR_RED : COLOR_DARKGRAY;
            display_tft_fill_rect(2, 20 + (i * 20), 124, 18, bg_color);
        }

        char buf[24];
        if (i == 0) snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[i], (long)s_config.min_start_sum);
        else if (i == 1) snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[i], (long)s_config.price_per_10min);
        else if (i == 2) snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[i], (long)s_config.total_money);
        else snprintf(buf, sizeof(buf), "%s", MENU_LABELS[i]);

        display_tft_draw_string(4, 22 + (i * 20), buf, text_color, bg_color);
    }
}

// Главная функция обработки событий энкодера для меню
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
                display_tft_sleep();
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
        .clock_speed_hz = 20 * 1000 * 1000,
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
