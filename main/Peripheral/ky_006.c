#include "ky_006.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "KY_006";

#define LEDC_TIMER              LEDC_TIMER_1
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_7
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT 
#define LEDC_DUTY               (250)             // Немного увеличили громкость для заметности предупреждения
#define LEDC_FREQUENCY          (1000)            

#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_C6  1046
#define NOTE_D6  1175

typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} note_t;

static volatile bool s_is_playing = false;

static void buzzer_task(void *pvParameters) {
    s_is_playing = true;

    // Настраиваем ШИМ на время воспроизведения мелодии
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = KY_006_GPIO_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    // Более длинная и заметная тревожная мелодия (сигнал предупреждения)
    note_t melody[] = {
        {NOTE_C6, 120},
        {0,       60 },
        {NOTE_G5, 120},
        {0,       60 },
        {NOTE_C6, 120},
        {0,       80 },
        {NOTE_D6, 200},
        {0,       100},
        {NOTE_C6, 300},
    };

    int len = sizeof(melody) / sizeof(melody[0]);

    for (int i = 0; i < len; i++) {
        if (melody[i].freq == 0) {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        } else {
            ledc_set_freq(LEDC_MODE, LEDC_TIMER, melody[i].freq);
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        }
        vTaskDelay(pdMS_TO_TICKS(melody[i].duration_ms));
    }

    // Останавливаем ШИМ
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);

    // Возвращаем пин в режим ВХОДА (высокоомный режим)
    gpio_reset_pin(KY_006_GPIO_PIN);
    gpio_set_direction(KY_006_GPIO_PIN, GPIO_MODE_INPUT);

    s_is_playing = false;
    vTaskDelete(NULL);
}

void ky_006_init(void) {
    ESP_LOGI(TAG, "Инициализация модуля MH-FMD (KY-006)...");
    s_is_playing = false;

    // При старте сразу ставим пин в режим ВХОДА
    gpio_reset_pin(KY_006_GPIO_PIN);
    gpio_set_direction(KY_006_GPIO_PIN, GPIO_MODE_INPUT);
}

void ky_006_play_warning_melody(void) {
    if (s_is_playing) return;
    xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 4, NULL);
}
