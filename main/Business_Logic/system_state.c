#include "system_state.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static int g_balance = 0;
static SemaphoreHandle_t g_state_mutex = NULL;

void system_state_init(void) {
    g_state_mutex = xSemaphoreCreateMutex();
    g_balance = 0;
}

void system_state_add_pulses(int pulses) {
    if (pulses <= 0) return;

    int added_soms = pulses * SOM_PER_PULSE;

    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_balance += added_soms;
        printf("[SYSTEM] Импульсов: %d | Добавлено: %d сом | Новый баланс: %d сом\n", 
               pulses, added_soms, g_balance);
        xSemaphoreGive(g_state_mutex);
    }
}

int system_state_get_balance(void) {
    int current_balance = 0;
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        current_balance = g_balance;
        xSemaphoreGive(g_state_mutex);
    }
    return current_balance;
}
