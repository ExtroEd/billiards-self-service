#include "display_tft.h"
#include "UI/font5x7.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY_TFT";
static spi_device_handle_t s_spi_dev = NULL;

static bool s_is_awake = true;
static int64_t s_last_activity_time = 0;
#define SLEEP_TIMEOUT_US (60 * 1000 * 1000LL) // 60 сек

#define ST7735_OFFSET_X  2
#define ST7735_OFFSET_Y  1

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
    ESP_LOGI(TAG, "ST7735 driver initialized successfully.");
}
