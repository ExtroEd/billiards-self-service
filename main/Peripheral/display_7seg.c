#include "display_7seg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static uint8_t s_brightness = 0x0f; // Максимальная яркость

// Таблица сегментов (0-9)
static const uint8_t digit_to_segment[] = {
    0x3f, // 0
    0x06, // 1
    0x5b, // 2
    0x4f, // 3
    0x66, // 4
    0x6d, // 5
    0x7d, // 6
    0x07, // 7
    0x7f, // 8
    0x6f  // 9
};

static void delay_us(uint32_t us) {
    ets_delay_us(us);
}

static void tm1637_start(void) {
    gpio_set_level(TM1637_DIO_PIN, 0);
    delay_us(5);
    gpio_set_level(TM1637_CLK_PIN, 0);
    delay_us(5);
}

static void tm1637_stop(void) {
    gpio_set_level(TM1637_CLK_PIN, 0);
    gpio_set_level(TM1637_DIO_PIN, 0);
    delay_us(5);
    gpio_set_level(TM1637_CLK_PIN, 1);
    gpio_set_level(TM1637_DIO_PIN, 1);
    delay_us(5);
}

static bool tm1637_write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        gpio_set_level(TM1637_CLK_PIN, 0);
        gpio_set_level(TM1637_DIO_PIN, (byte >> i) & 0x01);
        delay_us(5);
        gpio_set_level(TM1637_CLK_PIN, 1);
        delay_us(5);
    }

    // Чтение ACK
    gpio_set_level(TM1637_CLK_PIN, 0);
    gpio_set_direction(TM1637_DIO_PIN, GPIO_MODE_INPUT);
    delay_us(5);
    gpio_set_level(TM1637_CLK_PIN, 1);
    bool ack = (gpio_get_level(TM1637_DIO_PIN) == 0);
    gpio_set_direction(TM1637_DIO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TM1637_CLK_PIN, 0);
    delay_us(5);

    return ack;
}

void display_7seg_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TM1637_CLK_PIN) | (1ULL << TM1637_DIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(TM1637_CLK_PIN, 1);
    gpio_set_level(TM1637_DIO_PIN, 1);
    
    display_7seg_clear();
}

void display_7seg_clear(void) {
    uint8_t blank_data[4] = {0x00, 0x00, 0x00, 0x00};
    
    tm1637_start();
    tm1637_write_byte(0x40); // Авто-инкремент адреса
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0xC0); // Начать с первого разряда
    for (int i = 0; i < 4; i++) {
        tm1637_write_byte(blank_data[i]);
    }
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0x88 | s_brightness); // Включение дисплея
    tm1637_stop();
}

void display_7seg_show_time(int minutes, int seconds, bool show_colon) {
    uint8_t data[4];

    data[0] = digit_to_segment[(minutes / 10) % 10];
    data[1] = digit_to_segment[minutes % 10];
    if (show_colon) {
        data[1] |= 0x80; // Включение двоеточия (старший бит во 2 разряде)
    }

    data[2] = digit_to_segment[(seconds / 10) % 10];
    data[3] = digit_to_segment[seconds % 10];

    tm1637_start();
    tm1637_write_byte(0x40);
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0xC0);
    for (int i = 0; i < 4; i++) {
        tm1637_write_byte(data[i]);
    }
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0x88 | s_brightness);
    tm1637_stop();
}

void display_7seg_show_number(int value) {
    if (value < 0) value = 0;
    if (value > 9999) value = 9999;

    int minutes = value;
    int seconds = 0;

    display_7seg_show_time(minutes, seconds, false);
}

// Структура одного кадра змейки (активный сегмент + разряд 0..3)
typedef struct {
    uint8_t digit;
    uint8_t segment;
} snake_frame_t;

// 12 шагов по внешнему контуру индикатора
static const snake_frame_t snake_path[] = {
    {0, 0x01}, // Разряд 0: Верх (a)
    {1, 0x01}, // Разряд 1: Верх (a)
    {2, 0x01}, // Разряд 2: Верх (a)
    {3, 0x01}, // Разряд 3: Верх (a)
    {3, 0x02}, // Разряд 3: Право-Верх (b)
    {3, 0x04}, // Разряд 3: Право-Низ (c)
    {3, 0x08}, // Разряд 3: Низ (d)
    {2, 0x08}, // Разряд 2: Низ (d)
    {1, 0x08}, // Разряд 1: Низ (d)
    {0, 0x08}, // Разряд 0: Низ (d)
    {0, 0x10}, // Разряд 0: Лево-Низ (e)
    {0, 0x20}  // Разряд 0: Лево-Верх (f)
};

static uint8_t s_snake_step = 0;

void display_7seg_snake_step(void) {
    uint8_t raw_data[4] = {0, 0, 0, 0};
    
    // Включаем нужный сегмент на нужной цифре
    raw_data[snake_path[s_snake_step].digit] = snake_path[s_snake_step].segment;

    // Переходим к следующему шагу
    s_snake_step = (s_snake_step + 1) % (sizeof(snake_path) / sizeof(snake_path[0]));

    tm1637_start();
    tm1637_write_byte(0x40);
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0xC0);
    for (int i = 0; i < 4; i++) {
        tm1637_write_byte(raw_data[i]);
    }
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0x88 | s_brightness);
    tm1637_stop();
}
