#ifndef KY_006_H
#define KY_006_H

#include "driver/gpio.h"

// Пин подключения сигнала (I/O) зуммера на ESP32
#define KY_006_GPIO_PIN GPIO_NUM_25

void ky_006_init(void);
void ky_006_play_warning_melody(void);

#endif // KY_006_H
