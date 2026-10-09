#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

typedef struct i2c_master_dev_t *i2c_master_dev_handle_t;

#define RELAY_GPIO_PIN GPIO_NUM_17

void system_state_init(i2c_master_dev_handle_t ds3231_dev);
void system_state_add_credit(int amount);
int system_state_get_balance(void);
int system_state_get_remaining_seconds(void);
void system_state_reset_balance(void);
void system_state_set_min_threshold(int min_soms);
int system_state_get_min_threshold(void);
void system_state_set_price_per_1hour(int price);
int system_state_get_price_per_1hour(void);
int32_t system_state_get_total_money(void);
void system_state_reset_total_money(void);
bool system_state_is_relay_active(void);
void system_state_init_sntp(void);
void system_state_handle_mqtt_cmd(const char *topic, int topic_len, const char *data, int data_len);
bool system_state_is_rtc_ok(void);

#endif // SYSTEM_STATE_H
