#include "config/ConfigTypes.h"

namespace ot {

namespace {
template <typename E, size_t N>
bool parseTable(const char* s, const char* const (&names)[N], E& out) {
    if (!s) return false;
    for (size_t i = 0; i < N; ++i) {
        if (strEqualsI(s, names[i])) {
            out = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

const char* const kBoard[] = {"UNKNOWN", "ESP32", "ESP32_S3"};
const char* const kBackend[] = {"NONE",   "ESP32_INTERNAL_DAC", "MAX98357A", "PCM5102A",
                                "ES8388", "WM8960",             "TAS5760M"};
const char* const kAmp[] = {"NONE", "MAX98357_INTERNAL", "TPA3118D2", "TAS5760_INTERNAL", "CUSTOM"};
const char* const kSpeaker[] = {"VISATON_FRS5_XTS", "DAYTON_CE70PR4", "VISATON_FRS8M",
                                "MONACOR_SPX30M", "CUSTOM"};
const char* const kCoupling[] = {"OPEN", "SEALED_CHAMBER", "CUSTOM_CHAMBER"};
const char* const kEngine[] = {"SINE", "ADDITIVE", "WAVETABLE", "HYBRID"};
const char* const kActuator[] = {"SERVO", "SOLENOID", "DISABLED"};
const char* const kServoDriver[] = {"ESP32_PWM", "PCA9685"};
const char* const kValveMode[] = {"AUTO", "MANUAL", "MIDI_CC", "DISABLED"};
const char* const kPriority[] = {"LAST", "HIGH", "LOW"};
const char* const kInstrument[] = {"BB_TRUMPET", "C_TRUMPET", "EB_TRUMPET", "CUSTOM"};
const char* const kPitchMode[] = {"CONCERT", "WRITTEN"};
const char* const kWifiMode[] = {"AP", "STA", "AP_STA"};
const char* const kVibSource[] = {"CC1", "AFTERTOUCH", "AUTOMATIC", "OFF"};
const char* const kVelCurve[] = {"LINEAR", "SOFT", "HARD", "FIXED"};
const char* const kMidiPort[] = {"NONE", "USB",          "BLE",          "RTP",   "DIN",
                                 "WEB",  "SOUND_ENGINE", "VALVE_ENGINE", "MONITOR"};

template <typename E, size_t N>
const char* nameOf(E v, const char* const (&names)[N]) {
    size_t i = static_cast<size_t>(v);
    return i < N ? names[i] : names[0];
}
}  // namespace

const char* toString(BoardType v) { return nameOf(v, kBoard); }
const char* toString(AudioBackendType v) { return nameOf(v, kBackend); }
const char* toString(AmplifierType v) { return nameOf(v, kAmp); }
const char* toString(SpeakerProfileId v) { return nameOf(v, kSpeaker); }
const char* toString(AcousticCouplingType v) { return nameOf(v, kCoupling); }
const char* toString(SynthEngineType v) { return nameOf(v, kEngine); }
const char* toString(ValveActuatorType v) { return nameOf(v, kActuator); }
const char* toString(ServoDriverType v) { return nameOf(v, kServoDriver); }
const char* toString(ValveMode v) { return nameOf(v, kValveMode); }
const char* toString(NotePriority v) { return nameOf(v, kPriority); }
const char* toString(InstrumentType v) { return nameOf(v, kInstrument); }
const char* toString(PitchInterpretation v) { return nameOf(v, kPitchMode); }
const char* toString(WifiMode v) { return nameOf(v, kWifiMode); }
const char* toString(VibratoSource v) { return nameOf(v, kVibSource); }
const char* toString(VelocityCurve v) { return nameOf(v, kVelCurve); }
const char* toString(MidiPort v) { return nameOf(v, kMidiPort); }

bool parseEnum(const char* s, BoardType& o) { return parseTable(s, kBoard, o); }
bool parseEnum(const char* s, AudioBackendType& o) { return parseTable(s, kBackend, o); }
bool parseEnum(const char* s, AmplifierType& o) { return parseTable(s, kAmp, o); }
bool parseEnum(const char* s, SpeakerProfileId& o) { return parseTable(s, kSpeaker, o); }
bool parseEnum(const char* s, AcousticCouplingType& o) { return parseTable(s, kCoupling, o); }
bool parseEnum(const char* s, SynthEngineType& o) { return parseTable(s, kEngine, o); }
bool parseEnum(const char* s, ValveActuatorType& o) { return parseTable(s, kActuator, o); }
bool parseEnum(const char* s, ServoDriverType& o) { return parseTable(s, kServoDriver, o); }
bool parseEnum(const char* s, ValveMode& o) { return parseTable(s, kValveMode, o); }
bool parseEnum(const char* s, NotePriority& o) { return parseTable(s, kPriority, o); }
bool parseEnum(const char* s, InstrumentType& o) { return parseTable(s, kInstrument, o); }
bool parseEnum(const char* s, PitchInterpretation& o) { return parseTable(s, kPitchMode, o); }
bool parseEnum(const char* s, WifiMode& o) { return parseTable(s, kWifiMode, o); }
bool parseEnum(const char* s, VibratoSource& o) { return parseTable(s, kVibSource, o); }
bool parseEnum(const char* s, VelocityCurve& o) { return parseTable(s, kVelCurve, o); }
bool parseEnum(const char* s, MidiPort& o) { return parseTable(s, kMidiPort, o); }

}  // namespace ot
