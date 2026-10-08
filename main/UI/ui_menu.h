#ifndef UI_MENU_H
#define UI_MENU_H

#include "Peripheral/encoder.h"
#include "qrcode.h"

void ui_menu_init(void);
void menu_process_event(encoder_event_t event);
void display_tft_draw_qrcode(esp_qrcode_handle_t qrcode, uint8_t scale);
void display_tft_show_qr_payload(const char *payload, const char *title);

#endif // UI_MENU_H
