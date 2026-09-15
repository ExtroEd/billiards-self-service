#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "Peripheral/encoder.h"

// Настройка пинов
#define DISPLAY_SCL_PIN   GPIO_NUM_14
#define DISPLAY_SDA_PIN   GPIO_NUM_23
#define DISPLAY_RES_PIN   GPIO_NUM_4
#define DISPLAY_DC_PIN    GPIO_NUM_26
#define DISPLAY_CS_PIN    GPIO_NUM_5
#define DISPLAY_BL_PIN    GPIO_NUM_15

#define DISPLAY_WIDTH     128
#define DISPLAY_HEIGHT    160

// Цвета в формате RGB565 (байты переставлены для ST7735)
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_CYAN        0x07FF
#define COLOR_DARKGRAY    0x39E7

// Структура параметров системы
typedef struct {
    int32_t min_start_sum;   // Мин. сумма старта (сом)
    int32_t price_per_10min; // Стоимость за 10 мин (сом)
    int32_t total_money;     // Общая касса (сом)
    int32_t current_balance; // Текущий зачисленный баланс
    int32_t remaining_time_s;// Оставшееся время (сек)
} device_config_t;

void display_tft_init(void);
void display_tft_set_backlight(bool enable);

// Графические примитивы
void display_tft_fill_screen(uint16_t color);
void display_tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void display_tft_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, uint16_t bg_color);
void display_tft_draw_char(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg_color);
// Логика работы экранов
void menu_process_event(encoder_event_t event);
void display_show_main_screen(void);
void display_tft_wake(void);
void display_tft_sleep(void);
void menu_process_event(encoder_event_t event);

#endif // DISPLAY_TFT_H
