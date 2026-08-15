// ============================================================================
//  ValveController.h - the valve engine.
//
//  Receives MIDI from the router, decides which valves must be down and hands
//  the request to whichever driver owns each valve.  Because every valve is
//  looked up in a driver table, a servo/servo/solenoid instrument works with
//  no special case anywhere else in the firmware.
//
//  Modes:
//    AUTO     note -> FingeringEngine -> valve mask
//    MANUAL   only the web UI commands the valves
//    MIDI_CC  each valve follows its own control change
//    OFF      the valves are parked and ignore everything
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"
#include "midi/NoteStack.h"
#include "valves/FingeringEngine.h"
#include "valves/IValveActuator.h"
#include "valves/Pca9685Valve.h"
#include "valves/ServoValve.h"
#include "valves/SolenoidValve.h"

namespace ot {

struct ValveStatus {
    bool pressed = false;
    bool fault = false;
    const char* faultText = nullptr;
    ValveActuatorType type = ValveActuatorType::OFF;
    float angle = 0.0f;
    uint8_t dutyPercent = 0;
};

class ValveController final : public IMidiSink {
public:
    bool begin(const ValvesConfig& valves, const InstrumentConfig& instrument);
    void end();

    void onMidi(const MidiMessage& msg) override;
    // Called from the actuator task.
    void update();

    void panic();
    void releaseAll();
    void enableAfterPanic();

    void setMode(ValveMode mode);
    ValveMode mode() const { return cfg_.mode; }

    // MANUAL mode and hardware test page.
    bool manualSet(uint8_t valve, bool pressed);
    bool testPulse(uint8_t valve, uint16_t durationMs);
    bool calibrationPreview(uint8_t valve, uint16_t angle);

    uint8_t valveCount() const { return cfg_.count; }
    ValveStatus status(uint8_t valve) const;
    uint8_t currentMask() const { return currentMask_; }
    bool anyFault() const;

    FingeringEngine& fingering() { return fingering_; }
    const FingeringEngine& fingering() const { return fingering_; }

    // Exposed so the diagnostics page can show what the note maps to.
    uint8_t maskForNote(uint8_t midiNote) const;

private:
    void applyMask(uint8_t mask);
    IValveActuator* driverFor(uint8_t valve) const;

    ValvesConfig cfg_;
    FingeringEngine fingering_;
    NoteStack notes_;

    ServoValve servo_;
    Pca9685Valve pca_;
    SolenoidValve solenoid_;
    IValveActuator* drivers_[kMaxValves] = {nullptr};

    uint8_t currentMask_ = 0;
    uint8_t manualMask_ = 0;
    uint8_t ccMask_ = 0;
    uint32_t pulseUntilMs_[kMaxValves] = {0};
    uint8_t pulseMask_ = 0;
    bool stopped_ = false;
    bool started_ = false;
};

}  // namespace ot
