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

#define MENU_ITEMS_COUNT 8

void ui_menu_init(void) {
    display_tft_fill_screen(COLOR_BLACK);
    display_tft_fill_rect(0, 0, 128, 16, COLOR_BLUE);
    display_tft_draw_string(10, 4, "Состояние", COLOR_WHITE, COLOR_BLUE);

    const int item_height = 14; // Немного уменьшили шаг по высоте, чтобы 8 строк влезли на экран
    const int start_y = 19;

    for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
        int cur_y = start_y + (i * (item_height + 2));
        char buf[64];
        uint16_t text_color = COLOR_WHITE;

        if (i == 0) {
            snprintf(buf, sizeof(buf), "Мин.старт: %d", system_state_get_min_threshold());
        } else if (i == 1) {
            snprintf(buf, sizeof(buf), "Цена 1ч: %d", system_state_get_price_per_1hour());
        } else if (i == 2) {
            snprintf(buf, sizeof(buf), "Касса: %ld", (long)system_state_get_total_money());
        } else if (i == 3) {
            // 4) Статус реле (нейтральным белым цветом)
            bool relay_on = system_state_is_relay_active();
            snprintf(buf, sizeof(buf), "Реле: %s", relay_on ? "включен" : "выключен");
            text_color = COLOR_WHITE;
        } else if (i == 4) {
            // 5) Оставшееся количество времени
            int total_secs = system_state_get_remaining_seconds();
            int mins = total_secs / 60;
            int secs = total_secs % 60;
            snprintf(buf, sizeof(buf), "Остаток: %02d:%02d", mins, secs);
            text_color = (total_secs > 0) ? COLOR_GREEN : COLOR_WHITE;
        } else if (i == 5) {
            // 6) Качество сигнала Wi-Fi в дБм
            if (wifi_app_is_connected()) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    int rssi = ap_info.rssi;
                    snprintf(buf, sizeof(buf), "ВайФай: %d дБм", rssi);
                    if (rssi >= -67) {
                        text_color = COLOR_GREEN;
                    } else if (rssi >= -80) {
                        text_color = COLOR_YELLOW;
                    } else {
                        text_color = COLOR_RED;
                    }
                } else {
                    snprintf(buf, sizeof(buf), "ВайФай: Подключено");
                    text_color = COLOR_GREEN;
                }
            } else {
                snprintf(buf, sizeof(buf), "ВайФай: нет связи");
                text_color = COLOR_RED;
            }
        } else if (i == 6) {
            // 7) Работоспособность модуля DS3231
            bool rtc_ok = system_state_is_rtc_ok();
            if (rtc_ok) {
                snprintf(buf, sizeof(buf), "ДС3231: Работает");
                text_color = COLOR_GREEN;
            } else {
                snprintf(buf, sizeof(buf), "ДС3231: Не работает");
                text_color = COLOR_RED;
            }
        } else if (i == 7) {
            // 8) Связь с брокером (MQTT)
            bool mqtt_ok = mqtt_app_is_connected();
            if (mqtt_ok) {
                snprintf(buf, sizeof(buf), "Брокер: Есть связь");
                text_color = COLOR_GREEN;
            } else {
                snprintf(buf, sizeof(buf), "Брокер: Нет связи");
                text_color = COLOR_RED;
            }
        }

        display_tft_draw_string(4, cur_y, buf, text_color, COLOR_BLACK);
    }
}
