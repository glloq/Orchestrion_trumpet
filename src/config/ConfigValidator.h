// ============================================================================
//  ConfigValidator.h - refuse impossible instruments before they are saved.
//
//  Checks performed:
//    * the same GPIO used twice (I2S / I2C / UART / servo / solenoid / SD);
//    * flash and PSRAM pins, input-only pins, strapping pins;
//    * USB-MIDI requested on a board without native USB;
//    * internal DAC requested on a chip that has none;
//    * a speaker/amplifier pair that would exceed the driver's power;
//    * servo travel, solenoid duty and PWM values outside a safe range.
//
//  The result is a list of ERROR / WARNING / INFO entries that the web UI
//  shows before the save button becomes active.  Pure logic, unit tested.
// ============================================================================
#pragma once

#include "config/BoardCaps.h"
#include "config/ConfigTypes.h"

namespace ot {

enum class Severity : uint8_t { INFO = 0, WARNING, ERROR };

static constexpr uint8_t kMaxValidationIssues = 24;
static constexpr uint8_t kIssueTextLen = 96;

struct ValidationIssue {
    Severity severity = Severity::INFO;
    char field[24] = "";
    char message[kIssueTextLen] = "";
};

class ValidationReport {
public:
    void add(Severity severity, const char* field, const char* message);
    uint8_t count() const { return count_; }
    const ValidationIssue& issue(uint8_t index) const { return issues_[index]; }
    bool hasErrors() const;
    bool hasWarnings() const;
    void clear() { count_ = 0; truncated_ = false; }
    bool truncated() const { return truncated_; }

private:
    ValidationIssue issues_[kMaxValidationIssues];
    uint8_t count_ = 0;
    bool truncated_ = false;
};

class ConfigValidator {
public:
    // `caps` is injected so the validator can be tested for both boards.
    static void validate(const InstrumentConfiguration& cfg, const BoardCapabilities& caps,
                         ValidationReport& out);
    static void validate(const InstrumentConfiguration& cfg, ValidationReport& out) {
        validate(cfg, boardCaps(), out);
    }

    // Applies the minimum repairs that make a configuration bootable.  Used
    // when a stored file is damaged: better a safe instrument than none.
    static bool sanitise(InstrumentConfiguration& cfg, const BoardCapabilities& caps);

    // Clamp a voicing into the range the DSP can actually run.  This is the
    // guard on the LIVE path: /api/audio/preview hands whatever the browser
    // sent straight to the audio task, so a NaN, a negative attack or a
    // 400 dB trim has to be caught here and not by the limiter.
    //
    // It never touches anything outside the voicing.  Impedance, power
    // limits, the hard ceiling, the pins and the backend are not reachable
    // from a preview by construction: they are not in this structure.
    static void sanitiseVoicing(VoicingConfig& voicing);

    static const char* toString(Severity s);
};

}  // namespace ot
