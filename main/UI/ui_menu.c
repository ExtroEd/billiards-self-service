#include "UI/ui_menu.h"
#include "Peripheral/display_tft.h"
#include "Peripheral/ds3231.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <string.h>
#include <stdio.h>
#include "Networking_Services/wifi_app.h"
#include "Networking_Services/app_mqtt_client.h"
#include "system_state.h"

static const char *TAG = "UI_MENU";

typedef enum {
    UI_STATE_MENU_NAV,   
    UI_STATE_MENU_EDIT,
    UI_STATE_SHOW_QR,
    UI_STATE_SHOW_CASH,
    UI_STATE_SHOW_STATS,
    UI_STATE_CONFIRM_RESET_CASH,
    UI_STATE_CONFIRM_RESET_TIME
} ui_state_t;

static ui_state_t s_ui_state = UI_STATE_MENU_NAV;
static int8_t s_selected_item = 0;
static char s_qr_current_title[32] = {0};

#define MENU_ITEMS_COUNT 8
#define VISIBLE_MENU_ITEMS 6 

static const char *MENU_LABELS[MENU_ITEMS_COUNT] = {
    "1.Вай-Фай",
    "2.Мин.старт",
    "3.Цена 1час",
    "4.Касса всего",
    "5.Сбр. кассы",
    "6.Сбр. времени",
    "7.+10 сом",
    "8.Статистика"
};

static void render_menu(void);
static void qrcode_display_cb(esp_qrcode_handle_t qrcode);

static void render_cash_screen(void) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
    display_tft_draw_string(24, 4, "Касса всего", COLOR_WHITE, COLOR_BLUE);

    display_tft_fill_rect(8, 35, 112, 45, COLOR_DARKGRAY);

    char cash_str[32];
    snprintf(cash_str, sizeof(cash_str), "%ld сом", (long)system_state_get_total_money());
    display_tft_draw_string(14, 52, cash_str, COLOR_GREEN, COLOR_DARKGRAY);

    display_tft_draw_string(8, 100, "Нажми для выхода", COLOR_WHITE, COLOR_BLACK);
}

static void render_stats_screen(void) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
    display_tft_draw_string(20, 4, "Статистика", COLOR_WHITE, COLOR_BLUE);

    // 1. Аптайм
    int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
    int days = (int)(uptime_sec / 86400);
    int hours = (int)((uptime_sec % 86400) / 3600);
    int mins = (int)((uptime_sec % 3600) / 60);

    display_tft_draw_string(4, 22, "Аптайм:", COLOR_YELLOW, COLOR_BLACK);
    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "%dд %02dч %02dм", days, hours, mins);
    display_tft_draw_string(52, 22, uptime_str, COLOR_YELLOW, COLOR_BLACK);

    // 2. Вай-Фай и уровень сигнала (RSSI в dBm)
    bool wifi_ok = (wifi_app_get_mode() == WIFI_APP_MODE_STA);
    display_tft_draw_string(4, 42, "Вай-Фай:", COLOR_WHITE, COLOR_BLACK);
    
    if (wifi_ok) {
        wifi_ap_record_t ap_info;
        int8_t rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
        char wifi_str[16];
        snprintf(wifi_str, sizeof(wifi_str), "%d дБм", rssi);
        display_tft_draw_string(52, 42, wifi_str, COLOR_GREEN, COLOR_BLACK);
    } else {
        display_tft_draw_string(52, 42, "Нет связи", COLOR_RED, COLOR_BLACK);
    }

    // 3. Брокер MQTT
    bool mqtt_ok = mqtt_app_is_connected();
    display_tft_draw_string(4, 62, "Брокер:", COLOR_WHITE, COLOR_BLACK);
    display_tft_draw_string(52, 62, mqtt_ok ? "Работает" : "Нет связи", 
                            mqtt_ok ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);

    // 4. Модуль RTC DS3231
    bool rtc_ok = system_state_is_rtc_ok();
    display_tft_draw_string(4, 82, "ДС3231:", COLOR_WHITE, COLOR_BLACK);
    display_tft_draw_string(52, 82, rtc_ok ? "Работает" : "Нет связи", 
                            rtc_ok ? COLOR_GREEN : COLOR_RED, COLOR_BLACK);

    display_tft_draw_string(8, 140, "Нажми для выхода", COLOR_DARKGRAY, COLOR_BLACK);
}

static void render_confirm_screen(const char *title) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_RED);
    display_tft_draw_string(8, 4, title, COLOR_WHITE, COLOR_RED);

    display_tft_draw_string(16, 45, "Вы уверены?", COLOR_YELLOW, COLOR_BLACK);
    display_tft_draw_string(10, 85, "Клик - ДА", COLOR_GREEN, COLOR_BLACK);
    display_tft_draw_string(10, 110, "Вращение - НЕТ", COLOR_RED, COLOR_BLACK);
}

static void render_menu(void) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
    display_tft_draw_string(10, 4, "Настройки", COLOR_WHITE, COLOR_BLUE);

    const int item_height = 18;
    const int start_y = 20;

    int top_index = 0;
    if (s_selected_item >= VISIBLE_MENU_ITEMS) {
        top_index = s_selected_item - VISIBLE_MENU_ITEMS + 1;
    }

    for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
        int item_idx = top_index + i;
        if (item_idx >= MENU_ITEMS_COUNT) break;

        uint16_t text_color = COLOR_WHITE;
        uint16_t bg_color = COLOR_BLACK;

        if (item_idx == s_selected_item) {
            bg_color = (s_ui_state == UI_STATE_MENU_EDIT) ? COLOR_RED : COLOR_DARKGRAY;
        }

        int cur_y = start_y + (i * (item_height + 2));

        display_tft_fill_rect(2, cur_y, 124, item_height, bg_color);

        char buf[24];
        if (item_idx == 0) {
            snprintf(buf, sizeof(buf), "%s", MENU_LABELS[item_idx]);
        } else if (item_idx == 1) {
            snprintf(buf, sizeof(buf), "%s:%d", MENU_LABELS[item_idx], system_state_get_min_threshold());
        } else if (item_idx == 2) {
            snprintf(buf, sizeof(buf), "%s:%d", MENU_LABELS[item_idx], system_state_get_price_per_1hour());
        } else if (item_idx == 3) {
            snprintf(buf, sizeof(buf), "%s:%ld", MENU_LABELS[item_idx], (long)system_state_get_total_money());
        } else {
            snprintf(buf, sizeof(buf), "%s", MENU_LABELS[item_idx]);
        }

        display_tft_draw_string(4, cur_y + 4, buf, text_color, bg_color);
    }
}

void ui_menu_init(void) {
    render_menu();
}

void menu_process_event(encoder_event_t event) {
    display_tft_wake();

    // --- Обработка выхода из экрана QR-кода ---
    if (s_ui_state == UI_STATE_SHOW_QR) {
        if (event == ENCODER_EVENT_CLICK || event == ENCODER_EVENT_LONG_PRESS) {
            ESP_LOGI(TAG, "Выход из экрана QR-кода. Возврат к рабочей сети Wi-Fi...");
            
            // Завершаем SoftAP и подключаемся обратно к Wi-Fi из NVS / secrets.h
            wifi_app_stop_ap_and_reconnect();

            s_ui_state = UI_STATE_MENU_NAV;
            render_menu();
        }
        return;
    }

    // --- Обработка выходов из кассы и статистики ---
    if (s_ui_state == UI_STATE_SHOW_CASH || s_ui_state == UI_STATE_SHOW_STATS) {
        if (event == ENCODER_EVENT_CLICK || event == ENCODER_EVENT_LONG_PRESS) {
            s_ui_state = UI_STATE_MENU_NAV;
            render_menu();
        }
        return;
    }

    if (s_ui_state == UI_STATE_CONFIRM_RESET_CASH || s_ui_state == UI_STATE_CONFIRM_RESET_TIME) {
        if (event == ENCODER_EVENT_CLICK) {
            if (s_ui_state == UI_STATE_CONFIRM_RESET_CASH) {
                system_state_reset_total_money();
            } else if (s_ui_state == UI_STATE_CONFIRM_RESET_TIME) {
                system_state_reset_balance();
            }
            s_ui_state = UI_STATE_MENU_NAV;
            render_menu();
        } else if (event == ENCODER_EVENT_UP || event == ENCODER_EVENT_DOWN || event == ENCODER_EVENT_LONG_PRESS) {
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
            if (s_selected_item == 0) {
                ESP_LOGI(TAG, "Открытие QR-кода для настройки Wi-Fi...");
                wifi_app_start_ap_mode(); 
                display_tft_show_qr_payload("WIFI:S:ESP32_Config;T:WPA;P:12345678;;", "ВАЙ-ФАЙ");
                return;
            } else if (s_selected_item == 1 || s_selected_item == 2) {
                s_ui_state = UI_STATE_MENU_EDIT;
            } else if (s_selected_item == 3) {
                s_ui_state = UI_STATE_SHOW_CASH;
                render_cash_screen();
                return;
            } else if (s_selected_item == 4) {
                s_ui_state = UI_STATE_CONFIRM_RESET_CASH;
                render_confirm_screen("Сброс кассы");
                return;
            } else if (s_selected_item == 5) {
                s_ui_state = UI_STATE_CONFIRM_RESET_TIME;
                render_confirm_screen("Сброс времени");
                return;
            } else if (s_selected_item == 6) {
                system_state_add_credit(10);
            } else if (s_selected_item == 7) {
                s_ui_state = UI_STATE_SHOW_STATS;
                render_stats_screen();
                return;
            }
            render_menu();
        }
    } 
    else if (s_ui_state == UI_STATE_MENU_EDIT) {
        if (s_selected_item == 1) {
            int th = system_state_get_min_threshold();
            if (event == ENCODER_EVENT_UP && th < 1000) {
                system_state_set_min_threshold(th + 1);
            }
            if (event == ENCODER_EVENT_DOWN && th > 1) {
                system_state_set_min_threshold(th - 1);
            }
        } else if (s_selected_item == 2) {
            int price = system_state_get_price_per_1hour();
            if (event == ENCODER_EVENT_UP && price < 5000) system_state_set_price_per_1hour(price + 5);
            if (event == ENCODER_EVENT_DOWN && price > 5) system_state_set_price_per_1hour(price - 5);
        }

        if (event == ENCODER_EVENT_CLICK) {
            s_ui_state = UI_STATE_MENU_NAV;
        }
        render_menu();
    }
}

void display_tft_draw_qrcode(esp_qrcode_handle_t qrcode, uint8_t scale) {
    if (!qrcode) return;

    int qr_size = esp_qrcode_get_size(qrcode);
    uint16_t qr_size_px = qr_size * scale;
    
    if (qr_size_px > DISPLAY_WIDTH || qr_size_px > DISPLAY_HEIGHT) {
        ESP_LOGE(TAG, "QR-код слишком большой для экрана!");
        return;
    }

    int16_t start_x = (DISPLAY_WIDTH - qr_size_px) / 2;
    int16_t start_y = 22 + ((DISPLAY_HEIGHT - 22 - 20 - qr_size_px) / 2);

    display_tft_fill_screen(COLOR_WHITE);

    if (s_qr_current_title[0] != '\0') {
        display_tft_fill_rect(0, 0, DISPLAY_WIDTH, 16, COLOR_BLUE);
        int len = strlen(s_qr_current_title);
        int text_x = (DISPLAY_WIDTH - (len * 6)) / 2;
        if (text_x < 0) text_x = 0;
        display_tft_draw_string(text_x, 4, s_qr_current_title, COLOR_WHITE, COLOR_BLUE);
    }

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

    display_tft_draw_string(14, 146, "Нажми для вых.", COLOR_DARKGRAY, COLOR_WHITE);
}

void display_tft_show_qr_payload(const char *payload, const char *title) {
    s_ui_state = UI_STATE_SHOW_QR;

    if (title) {
        strncpy(s_qr_current_title, title, sizeof(s_qr_current_title) - 1);
        s_qr_current_title[sizeof(s_qr_current_title) - 1] = '\0';
    } else {
        s_qr_current_title[0] = '\0';
    }

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qrcode_display_cb;
    cfg.max_qrcode_version = 10;

    esp_err_t err = esp_qrcode_generate(&cfg, payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка генерации QR (%s): %d", payload, err);
    }
}

static void qrcode_display_cb(esp_qrcode_handle_t qrcode) {
    display_tft_draw_qrcode(qrcode, 3);
}
