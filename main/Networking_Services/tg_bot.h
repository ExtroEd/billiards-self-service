#ifndef TG_BOT_H
#define TG_BOT_H

#include "esp_err.h"
#include "secrets.h"

/**
 * @brief Сформировать и мгновенно отправить отчёт по текущему столу в Telegram
 * @param target_chat_id Кому отправить (ID Шефа или Разработчика)
 */
void tg_bot_send_status_report(const char *target_chat_id);

#endif // TG_BOT_H
