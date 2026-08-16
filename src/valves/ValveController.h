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

#include "core/RingBuffer.h"
#include "midi/IMidiTransport.h"
#include "midi/NoteStack.h"
#include "valves/FingeringEngine.h"
#include "valves/IValveActuator.h"
#include "valves/Pca9685Valve.h"
#include "valves/ServoValve.h"
#include "valves/SolenoidValve.h"

namespace ot {

// A command from another task, applied by the actuator task in update().
enum class ValveCommandType : uint8_t { MODE, MANUAL, PULSE, CALIBRATE };

struct ValveCommand {
    ValveCommandType type = ValveCommandType::MODE;
    uint8_t valve = 0;
    uint16_t value = 0;   // duration, angle, or the mode as a number
    bool flag = false;    // pressed
};

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

    // MANUAL mode and hardware test page. These act immediately and are only
    // safe to call from the actuator task (and from the host tests, which have
    // no tasks at all). The web layer runs on the network task and must use the
    // post* forms below.
    bool manualSet(uint8_t valve, bool pressed);
    bool testPulse(uint8_t valve, uint16_t durationMs);
    bool calibrationPreview(uint8_t valve, uint16_t angle);

    // Queued forms, for callers that are not the actuator task. They validate
    // what can be validated without touching the hardware - so the browser
    // still gets a truthful error - then hand the command to update(), which
    // applies it in order. Nothing outside the actuator task ever writes a
    // driver register or a mask.
    bool postMode(ValveMode mode);
    bool postManual(uint8_t valve, bool pressed);
    bool postPulse(uint8_t valve, uint16_t durationMs);
    bool postCalibration(uint8_t valve, uint16_t angle);
    // Commands dropped because the queue was full: shown on the diagnostics
    // page rather than silently swallowed.
    uint32_t droppedCommands() const { return commands_.dropped(); }

    uint8_t valveCount() const { return cfg_.count; }
    ValveStatus status(uint8_t valve) const;
    uint8_t currentMask() const { return currentMask_; }
    bool anyFault() const;

    FingeringEngine& fingering() { return fingering_; }
    const FingeringEngine& fingering() const { return fingering_; }

    // Exposed so the diagnostics page can show what the note maps to.
    uint8_t maskForNote(uint8_t midiNote) const;
    // What the current mode wants the valves to be, ignoring any test pulse.
    uint8_t desiredMask() const;

private:
    void applyMask(uint8_t mask);
    void applyCommand(const ValveCommand& cmd);
    IValveActuator* driverFor(uint8_t valve) const;

    ValvesConfig cfg_;
    FingeringEngine fingering_;
    NoteStack notes_;

    ServoValve servo_;
    Pca9685Valve pca_;
    SolenoidValve solenoid_;
    IValveActuator* drivers_[kMaxValves] = {nullptr};

    // Commands from the network task, applied by the actuator task.
    RingBuffer<ValveCommand, 16> commands_;

    uint8_t currentMask_ = 0;
    uint8_t manualMask_ = 0;
    uint8_t ccMask_ = 0;
    // CC 64. The sound engine holds the note after the key comes up, so the
    // pistons have to hold the fingering with it: a bore that changes under a
    // note that is still speaking is the one thing the two engines must never
    // disagree about. `sustainedMask_` is what the pedal is holding down, and
    // it is what `desiredMask()` answers while no key is pressed.
    bool sustainDown_ = false;
    uint8_t sustainedMask_ = 0;
    uint32_t pulseUntilMs_[kMaxValves] = {0};
    uint8_t pulseMask_ = 0;
    bool stopped_ = false;
    bool started_ = false;
};

}  // namespace ot
