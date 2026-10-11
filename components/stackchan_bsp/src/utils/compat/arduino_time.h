// ESP-IDF replacements for the Arduino timing calls used by the BSP,
// so the BSP builds without the Arduino core.
#pragma once
#include <cstdint>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

inline uint32_t millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

inline void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
