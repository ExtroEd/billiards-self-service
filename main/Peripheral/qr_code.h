#ifndef QR_CODE_H
#define QR_CODE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Отрисовывает QR-код Wi-Fi точки доступа на TFT дисплее
 * @param ssid Имя сети (SSID)
 * @param password Пароль от сети
 */
void display_show_wifi_qr(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif // QR_CODE_H
