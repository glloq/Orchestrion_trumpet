#include "core/SystemState.h"

#include "diagnostics/Logger.h"

namespace ot {

SystemState& SystemState::instance() {
    static SystemState state;
    return state;
}

void SystemState::setMode(RunMode mode) {
    if (status_.mode == mode) return;
    OT_LOGI("state", "%s -> %s", toString(status_.mode), toString(mode));
    status_.mode = mode;
}

void SystemState::raiseFault(FaultCode code, const char* text) {
    if (code == FaultCode::NONE) return;
    status_.faultMask |= (1u << static_cast<uint8_t>(code));
    if (text) copyString(status_.faultText, sizeof(status_.faultText), text);
    OT_LOGE("fault", "%s: %s", toString(code), text ? text : "");
}

void SystemState::clearFault(FaultCode code) {
    status_.faultMask &= ~(1u << static_cast<uint8_t>(code));
    if (status_.faultMask == 0) status_.faultText[0] = '\0';
}

void SystemState::clearAllFaults() {
    status_.faultMask = 0;
    status_.faultText[0] = '\0';
}

bool SystemState::hasFault(FaultCode code) const {
    return (status_.faultMask & (1u << static_cast<uint8_t>(code))) != 0;
}

uint32_t SystemState::uptimeSeconds() const {
    return (OT_MILLIS() - status_.bootTimeMs) / 1000u;
}

const char* SystemState::toString(RunMode mode) {
    switch (mode) {
        case RunMode::BOOTING:
            return "BOOTING";
        case RunMode::RUNNING:
            return "RUNNING";
        case RunMode::SAFE_MODE:
            return "SAFE_MODE";
        case RunMode::PANIC:
            return "PANIC";
        case RunMode::UPDATING:
            return "UPDATING";
        default:
            return "UNKNOWN";
    }
}

const char* SystemState::toString(FaultCode code) {
    switch (code) {
        case FaultCode::CONFIG_INVALID:
            return "CONFIG_INVALID";
        case FaultCode::AUDIO_BACKEND_FAILED:
            return "AUDIO_BACKEND_FAILED";
        case FaultCode::VALVE_DRIVER_FAILED:
            return "VALVE_DRIVER_FAILED";
        case FaultCode::SOLENOID_THERMAL:
            return "SOLENOID_THERMAL";
        case FaultCode::I2C_DEVICE_MISSING:
            return "I2C_DEVICE_MISSING";
        case FaultCode::FILESYSTEM:
            return "FILESYSTEM";
        case FaultCode::NONE:
        default:
            return "NONE";
    }
}

}  // namespace ot
