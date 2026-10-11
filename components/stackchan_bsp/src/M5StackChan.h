/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "utils/touch_sensor/touch_sensor.h"
#include "utils/motion/motion.h"
#include <M5GFX.h>
#include <M5Unified.hpp>
#include <memory>

namespace m5 {

class M5StackChan_Class {
public:
    void begin();
    void update();

    inline LGFX_Device& Display()
    {
        return M5.Display;
    }
    inline LGFX_Device& Lcd()
    {
        return M5.Lcd;
    }
    TouchSensor_Class TouchSensor;
    stackchan::motion::Motion Motion;

    /**
     * @brief Enable or disable servo power.
     *
     * @param enabled
     */
    void setServoPowerEnabled(bool enabled);

    /**
     * @brief Set the Rgb Color object.
     * There are 12 RGB LEDs, 0-5 are on the left, 6-11 are on the right.
     *
     * @param index
     * @param r
     * @param g
     * @param b
     */
    void setRgbColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Update the RGB LEDs with the set colors.
     *
     */
    void refreshRgb();

    /**
     * @brief Set all RGB LEDs to the specified color.
     *
     * @param r
     * @param g
     * @param b
     */
    void showRgbColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Get battery voltage.
     *
     * @return float
     */
    float getBatteryVoltage();

    /**
     * @brief Get battery current.
     * Positive when discharging, negative when charging.
     *
     * @return float
     */
    float getBatteryCurrent();

protected:
    void io_expander_init();
    void servo_init();
    void ina226_init();
};

}  // namespace m5

extern m5::M5StackChan_Class M5StackChan;
