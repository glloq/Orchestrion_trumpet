// ============================================================================
//  SystemState.h - the shared, read-mostly picture of the instrument.
//
//  Every task writes only its own fields and everyone reads the rest, so the
//  structure holds plain scalars: a torn read of a counter is harmless, and no
//  lock is ever taken on the audio or MIDI path.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

enum class RunMode : uint8_t {
    BOOTING = 0,
    RUNNING,
    SAFE_MODE,   // invalid configuration: web UI only, no actuators, no audio
    PANIC,       // everything parked, waiting for the user to release it
    UPDATING     // OTA in progress
};

enum class FaultCode : uint8_t {
    NONE = 0,
    CONFIG_INVALID,
    AUDIO_BACKEND_FAILED,
    VALVE_DRIVER_FAILED,
    SOLENOID_THERMAL,
    I2C_DEVICE_MISSING,
    FILESYSTEM,
    COUNT
};

struct SystemStatus {
    RunMode mode = RunMode::BOOTING;
    uint32_t bootTimeMs = 0;

    // ---- audio ----
    bool audioRunning = false;
    bool audioMuted = true;
    uint32_t audioUnderruns = 0;
    uint32_t audioBlocks = 0;
    float audioCpuPercent = 0.0f;
    uint16_t audioQueueDepth = 0;

    // ---- midi ----
    uint32_t midiRxTotal = 0;
    uint32_t midiTxTotal = 0;
    uint16_t midiRxPerSecond = 0;
    uint16_t midiTxPerSecond = 0;

    // ---- network ----
    bool apActive = false;
    bool staConnected = false;
    int8_t wifiRssi = 0;
    uint8_t webClients = 0;

    // ---- faults ----
    uint32_t faultMask = 0;
    char faultText[96] = "";
};

class SystemState {
public:
    static SystemState& instance();

    SystemStatus& mutableStatus() { return status_; }
    const SystemStatus& status() const { return status_; }

    void setMode(RunMode mode);
    RunMode mode() const { return status_.mode; }

    void raiseFault(FaultCode code, const char* text);
    void clearFault(FaultCode code);
    void clearAllFaults();
    bool hasFault(FaultCode code) const;
    bool anyFault() const { return status_.faultMask != 0; }

    uint32_t uptimeSeconds() const;
    static const char* toString(RunMode mode);
    static const char* toString(FaultCode code);

private:
    SystemState() = default;
    SystemStatus status_;
};

}  // namespace ot
