#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ENCODER";
static QueueHandle_t s_event_queue = NULL;

static volatile uint8_t s_encoder_state = 0;
static volatile int64_t s_last_step_time = 0;

// Накопительный счетчик микрошагов для деления на 4
static volatile int8_t s_pulse_counter = 0;

// Таблица переходов состояний энкодера
static const int8_t KNOBDIR[] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static void IRAM_ATTR encoder_isr_handler(void *arg) {
    int64_t current_time = esp_timer_get_time();
    
    // Микро-дебаунс (1 мс)
    if (current_time - s_last_step_time < 1000) {
        return;
    }

    uint8_t clk_val = gpio_get_level(ENCODER_CLK_PIN);
    uint8_t dt_val  = gpio_get_level(ENCODER_DT_PIN);

    s_encoder_state = ((s_encoder_state << 2) | (clk_val << 1) | dt_val) & 0x0F;

    int8_t direction = KNOBDIR[s_encoder_state];

    if (direction != 0) {
        s_last_step_time = current_time;
        s_pulse_counter += direction;

        // Отправляем событие ТОЛЬКО при накоплении 4 микрошагов (1 полный физический щелчок)
        if (s_pulse_counter >= 4 || s_pulse_counter <= -4) {
            encoder_event_t event = (s_pulse_counter >= 4) ? ENCODER_EVENT_UP : ENCODER_EVENT_DOWN;
            s_pulse_counter = 0; // Сбрасываем счетчик для следующего щелчка

            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(s_event_queue, &event, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

static void encoder_button_task(void *pvParameters) {
    int last_sw_state = 1;
    int64_t press_start_time = 0;

    while (1) {
        int sw_state = gpio_get_level(ENCODER_SW_PIN);

        if (last_sw_state == 1 && sw_state == 0) {
            press_start_time = esp_timer_get_time();
            vTaskDelay(pdMS_TO_TICKS(30));
        } 
        else if (last_sw_state == 0 && sw_state == 1) {
            int64_t press_duration = (esp_timer_get_time() - press_start_time) / 1000;
            
            encoder_event_t event = ENCODER_EVENT_NONE;
            if (press_duration >= 800) {
                event = ENCODER_EVENT_LONG_PRESS;
            } else if (press_duration >= 30) {
                event = ENCODER_EVENT_CLICK;
            }

            if (event != ENCODER_EVENT_NONE && s_event_queue != NULL) {
                xQueueSend(s_event_queue, &event, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }

        last_sw_state = sw_state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void encoder_init(QueueHandle_t event_queue) {
    s_event_queue = event_queue;
    s_pulse_counter = 0;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_CLK_PIN) | (1ULL << ENCODER_DT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io_conf);

    gpio_config_t sw_conf = {
        .pin_bit_mask = (1ULL << ENCODER_SW_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&sw_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_CLK_PIN, encoder_isr_handler, NULL);
    gpio_isr_handler_add(ENCODER_DT_PIN, encoder_isr_handler, NULL);

    uint8_t clk_val = gpio_get_level(ENCODER_CLK_PIN);
    uint8_t dt_val  = gpio_get_level(ENCODER_DT_PIN);
    s_encoder_state = (clk_val << 1) | dt_val;

    xTaskCreate(encoder_button_task, "encoder_btn_task", 2048, NULL, 10, NULL);

    ESP_LOGI(TAG, "Encoder initialized with 1:4 Divider (CLK:%d, DT:%d, SW:%d)", 
             ENCODER_CLK_PIN, ENCODER_DT_PIN, ENCODER_SW_PIN);
}
