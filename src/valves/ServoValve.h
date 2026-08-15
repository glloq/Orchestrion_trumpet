// ============================================================================
//  ServoValve.h - hobby servos driven by the ESP32 LEDC peripheral.
//
//  One LEDC channel per valve, 50 Hz, 16 bit resolution.  Motion is planned by
//  ServoMotion; when a movement is finished the channel is detached so the
//  servo stops buzzing, stops drawing current and stops heating.
// ============================================================================
#pragma once

#include "valves/IValveActuator.h"
#include "valves/ServoMotion.h"

namespace ot {

class ServoValve final : public IValveActuator {
public:
    // Declares that `valve` is handled by this driver.
    bool addValve(uint8_t valve, const ValveConfig& cfg, uint16_t frequencyHz);

    bool begin() override;
    void setValve(uint8_t valve, bool pressed) override;
    void releaseAll() override;
    void update() override;
    void emergencyStop() override;
    void enable() override;
    bool isPressed(uint8_t valve) const override;

    // Calibration helpers used by the web UI: move immediately, without
    // touching the stored configuration.
    void previewAngle(uint8_t valve, uint16_t angle);
    float currentAngle(uint8_t valve) const;

private:
    struct Slot {
        bool used = false;
        bool attached = false;
        int8_t pin = -1;
        ServoMotion motion;
    };

    void writePulse(Slot& slot);
    void detach(Slot& slot);

    Slot slots_[kMaxValves];
    uint16_t frequencyHz_ = 50;
    uint32_t lastUpdateMs_ = 0;
    bool stopped_ = false;
    bool started_ = false;
};

}  // namespace ot
