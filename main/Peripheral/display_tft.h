#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

// Настройка пинов согласно вашей конфигурации
#define DISPLAY_SCL_PIN   GPIO_NUM_14  // SPI Clock
#define DISPLAY_SDA_PIN   GPIO_NUM_23  // SPI MOSI
#define DISPLAY_RES_PIN   GPIO_NUM_4   // Reset
#define DISPLAY_DC_PIN    GPIO_NUM_2   // Data / Command
#define DISPLAY_CS_PIN    GPIO_NUM_5   // Chip Select
#define DISPLAY_BL_PIN    GPIO_NUM_15  // Backlight

// Разрешение дисплея ST7735 (128x160)
#define DISPLAY_WIDTH     128
#define DISPLAY_HEIGHT    160

/**
 * @brief Базовая инициализация шины SPI и пинов дисплея
 */
void display_tft_init(void);

/**
 * @brief Управление подсветкой дисплея
 */
void display_tft_set_backlight(bool enable);

#endif // DISPLAY_TFT_H