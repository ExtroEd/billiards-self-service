#include "ds3231.h"
#include <time.h>
#include "esp_log.h"

static const char *TAG = "DS3231";

#define DS3231_REG_TIME   0x00
#define DS3231_REG_STATUS 0x0F
#define DS3231_STAT_OSF   0x80
#define DS3231_REG_ALARM1_BASE 0x07
#define DS3231_REG_ALARM2_BASE 0x0B

static inline uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }
static inline uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }

esp_err_t ds3231_init(gpio_num_t sda_pin, gpio_num_t scl_pin, i2c_master_dev_handle_t *out_dev)
{
    if (!out_dev) return ESP_ERR_INVALID_ARG;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка создания I2C шины: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR,
        .scl_speed_hz = 100000, // 100 kHz
    };

    err = i2c_master_bus_add_device(bus_handle, &dev_config, out_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка добавления DS3231 на шину: %s", esp_err_to_name(err));
        return err;
    }

    // Быстрая проверка флага сбоя осциллятора (OSF)
    uint8_t reg = DS3231_REG_STATUS;
    uint8_t status = 0;
    if (i2c_master_transmit_receive(*out_dev, &reg, 1, &status, 1, 50) == ESP_OK) {
        if (status & DS3231_STAT_OSF) {
            ESP_LOGW(TAG, "Внимание: Был сбой питания RTC! Требуется синхронизация времени.");
        }
    }

    return ESP_OK;
}

esp_err_t ds3231_write_rem_seconds(i2c_master_dev_handle_t dev, int32_t seconds) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    uint8_t buf[5];
    buf[0] = DS3231_REG_ALARM1_BASE;
    buf[1] = (uint8_t)(seconds & 0xFF);
    buf[2] = (uint8_t)((seconds >> 8) & 0xFF);
    buf[3] = (uint8_t)((seconds >> 16) & 0xFF);
    buf[4] = (uint8_t)((seconds >> 24) & 0xFF);

    return i2c_master_transmit(dev, buf, sizeof(buf), 50);
}

esp_err_t ds3231_read_rem_seconds(i2c_master_dev_handle_t dev, int32_t *out_seconds) {
    if (!dev || !out_seconds) return ESP_ERR_INVALID_ARG;

    uint8_t reg = DS3231_REG_ALARM1_BASE;
    uint8_t buf[4] = {0};

    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, 4, 50);
    if (err != ESP_OK) return err;

    *out_seconds = (int32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
    return ESP_OK;
}

esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev, int64_t *out_time)
{
    if (!dev || !out_time) return ESP_ERR_INVALID_ARG;

    uint8_t reg = DS3231_REG_TIME;
    uint8_t data[7];

    // Транзакция: запись 1 байта адреса регистра, потом считывание 7 байт времени
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, data, 7, 50);
    if (err != ESP_OK) {
        return err;
    }

    struct tm tm_info = {
        .tm_sec  = bcd2bin(data[0] & 0x7F),
        .tm_min  = bcd2bin(data[1] & 0x7F),
        .tm_hour = bcd2bin(data[2] & 0x3F),
        .tm_mday = bcd2bin(data[4] & 0x3F),
        .tm_mon  = bcd2bin(data[5] & 0x1F) - 1,
        .tm_year = bcd2bin(data[6]) + 100, // 2000+ -> +100 к 1900
        .tm_isdst = -1,
    };

    *out_time = (int64_t)timegm(&tm_info);
    return ESP_OK;
}

esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev, int64_t unix_time)
{
    if (!dev) return ESP_ERR_INVALID_ARG;

    struct tm tm_info;
    gmtime_r((time_t *)&unix_time, &tm_info);

    uint8_t tx_buf[8];
    tx_buf[0] = DS3231_REG_TIME;
    tx_buf[1] = bin2bcd(tm_info.tm_sec);
    tx_buf[2] = bin2bcd(tm_info.tm_min);
    tx_buf[3] = bin2bcd(tm_info.tm_hour);
    tx_buf[4] = bin2bcd(tm_info.tm_wday + 1);
    tx_buf[5] = bin2bcd(tm_info.tm_mday);
    tx_buf[6] = bin2bcd(tm_info.tm_mon + 1);
    tx_buf[7] = bin2bcd(tm_info.tm_year % 100);

    esp_err_t err = i2c_master_transmit(dev, tx_buf, sizeof(tx_buf), 50);
    if (err != ESP_OK) return err;

    // Сброс флага сбоя питания OSF после успешной записи
    uint8_t reg = DS3231_REG_STATUS;
    uint8_t status = 0;
    if (i2c_master_transmit_receive(dev, &reg, 1, &status, 1, 50) == ESP_OK) {
        status &= ~DS3231_STAT_OSF;
        uint8_t clear_buf[2] = {DS3231_REG_STATUS, status};
        i2c_master_transmit(dev, clear_buf, 2, 50);
    }

    return ESP_OK;
}

esp_err_t ds3231_write_last_timestamp(i2c_master_dev_handle_t dev, int64_t timestamp) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    uint32_t ts32 = (uint32_t)timestamp;
    uint8_t buf[5];
    buf[0] = DS3231_REG_ALARM2_BASE;
    buf[1] = (uint8_t)(ts32 & 0xFF);
    buf[2] = (uint8_t)((ts32 >> 8) & 0xFF);
    buf[3] = (uint8_t)((ts32 >> 16) & 0xFF);
    buf[4] = (uint8_t)((ts32 >> 24) & 0xFF);

    return i2c_master_transmit(dev, buf, sizeof(buf), 50);
}

esp_err_t ds3231_read_last_timestamp(i2c_master_dev_handle_t dev, int64_t *out_timestamp) {
    if (!dev || !out_timestamp) return ESP_ERR_INVALID_ARG;

    uint8_t reg = DS3231_REG_ALARM2_BASE;
    uint8_t buf[4] = {0};

    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, 4, 50);
    if (err != ESP_OK) return err;

    uint32_t ts32 = (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
    *out_timestamp = (int64_t)ts32;
    return ESP_OK;
}
