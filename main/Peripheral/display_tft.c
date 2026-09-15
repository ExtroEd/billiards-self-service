#include "display_tft.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY_TFT";
static spi_device_handle_t s_spi_dev = NULL;

// Отправка команды в ST7735
static void display_tft_send_cmd(uint8_t cmd) {
    gpio_set_level(DISPLAY_DC_PIN, 0); // DC = 0 (Command)
    
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd
    };
    spi_device_polling_transmit(s_spi_dev, &t);
}

// Отправка данных в ST7735
static void display_tft_send_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    
    gpio_set_level(DISPLAY_DC_PIN, 1); // DC = 1 (Data)
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };
    spi_device_polling_transmit(s_spi_dev, &t);
}

void display_tft_set_backlight(bool enable) {
    gpio_set_level(DISPLAY_BL_PIN, enable ? 1 : 0);
}

void display_tft_init(void) {
    // 1. Конфигурация управляющих пинов (DC, RES, BL)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_DC_PIN) | 
                        (1ULL << DISPLAY_RES_PIN) | 
                        (1ULL << DISPLAY_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // 2. Инициализация аппаратной шины SPI2 (HSPI)
    spi_bus_config_t buscfg = {
        .miso_io_num = -1, // MISO не задействован
        .mosi_io_num = DISPLAY_SDA_PIN,
        .sclk_io_num = DISPLAY_SCL_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000, // 20 МГц
        .mode = 0,                           // SPI Mode 0
        .spics_io_num = DISPLAY_CS_PIN,
        .queue_size = 7
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_dev));

    // 3. Аппаратный сброс (Reset pulse)
    gpio_set_level(DISPLAY_RES_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(DISPLAY_RES_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. Включаем подсветку
    display_tft_set_backlight(true);

    ESP_LOGI(TAG, "TFT Display SPI interface initialized.");
}
