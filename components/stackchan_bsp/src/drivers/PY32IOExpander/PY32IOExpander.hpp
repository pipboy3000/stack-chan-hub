/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __M5_PY32IOEXPANDER_CLASS_H__
#define __M5_PY32IOEXPANDER_CLASS_H__

#include <M5Unified.hpp>

// M5Unified 0.2.21 changed the IOExpander_Base virtuals: they report the write
// status as bool, setPullMode takes gpio_pull_t and enablePull was removed.
#if defined(M5UNIFIED_VERSION_MAJOR) && \
    ((M5UNIFIED_VERSION_MAJOR > 0) || (M5UNIFIED_VERSION_MINOR > 2) || \
     (M5UNIFIED_VERSION_MINOR == 2 && M5UNIFIED_VERSION_PATCH >= 21))
#define PY32IOEXPANDER_STATUS_API 1
#else
#define PY32IOEXPANDER_STATUS_API 0
#endif

namespace m5 {
class PY32IOExpander_Class : public IOExpander_Base {
public:
    static constexpr std::uint8_t DEFAULT_ADDRESS = 0x6F;

#if !PY32IOEXPANDER_STATUS_API
    // Same names and values as IOExpander_Base::gpio_pull_t in M5Unified 0.2.21+
    enum gpio_pull_t : std::uint8_t { pull_none = 0, pull_up = 1, pull_down = 2 };
#endif

    PY32IOExpander_Class(std::uint8_t i2c_addr = DEFAULT_ADDRESS, std::uint32_t freq = 100000,
                         m5::I2C_Class* i2c = &m5::In_I2C)
        : IOExpander_Base(i2c_addr, freq, i2c)
    {
    }

    bool begin();

    // IOExpander_Base overrides
    // false input, true output
    // Return true when every register access was acknowledged.
#if PY32IOEXPANDER_STATUS_API
    bool setDirection(uint8_t pin, bool direction) override;

    bool setPullMode(uint8_t pin, gpio_pull_t mode) override;

    // false down, true up (kept for callers written against the old API)
    bool setPullMode(uint8_t pin, bool mode);

    // Kept for callers written against the old API: true enables the pull-up unless a pull
    // is already set, false disables both pulls.
    void enablePull(uint8_t pin, bool enablePull);

    bool setHighImpedance(uint8_t pin, bool enable) override;

    bool digitalWrite(uint8_t pin, bool level) override;

    bool resetIrq() override;

    bool disableIrq() override;

    bool enableIrq() override;
#else
    void setDirection(uint8_t pin, bool direction) override;

    void enablePull(uint8_t pin, bool enablePull) override;

    // false down, true up
    void setPullMode(uint8_t pin, bool mode) override;

    bool setPullMode(uint8_t pin, gpio_pull_t mode);

    void setHighImpedance(uint8_t pin, bool enable) override;

    void digitalWrite(uint8_t pin, bool level) override;

    void resetIrq() override;

    void disableIrq() override;

    void enableIrq() override;
#endif

    // false push-pull, true open-drain
    void setDriveMode(uint8_t pin, bool openDrain);

    bool getWriteValue(uint8_t pin) override;

    bool digitalRead(uint8_t pin) override;

    // Extended functionality
    uint16_t readDeviceUID();
    uint8_t readVersion();

    // ADC
    // channel: 1-4
    uint16_t analogRead(uint8_t channel);

    // PWM
    // channel: 0-3
    void setPwmDuty(uint8_t channel, uint8_t duty);
    void setPwmFrequency(uint16_t freq);

    // LED
    void setLedCount(uint8_t count);
    void setLedColor(uint8_t index, uint16_t color565);
    void setLedColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
    void setLedColor(uint8_t index, uint32_t color);
    void setLedData(const uint8_t* data, size_t len);
    void refreshLeds();

private:
    // Version independent implementation; the overrides above wrap these.
    bool _setDirection(uint8_t pin, bool direction);
    bool _setPullMode(uint8_t pin, gpio_pull_t mode);
    bool _setHighImpedance(uint8_t pin, bool enable);
    bool _digitalWrite(uint8_t pin, bool level);
    bool _resetIrq();
    bool _disableIrq();
    bool _enableIrq();

    static bool _isValidPin(uint8_t pin) { return pin < 14; }
    bool _writeBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin, bool value);
    bool _readBit(uint8_t reg_l, uint8_t reg_h, uint8_t pin);
};
}  // namespace m5

#endif
