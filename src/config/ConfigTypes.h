// ============================================================================
//  ConfigTypes.h - the complete, versioned description of one instrument.
//
//  This is plain data: no Arduino type, no driver, no I/O.  Every module of
//  the firmware receives the slice it needs and nothing else, which is what
//  keeps the MIDI router ignorant of the DAC and the sound engine ignorant of
//  the valve hardware.
// ============================================================================
#pragma once

#include "core/Platform.h"

namespace ot {

// Current on-disk schema.  Bump it and add a step in ConfigMigration whenever
// the meaning of an existing field changes.
static constexpr uint16_t kConfigSchemaVersion = 4;

static constexpr uint8_t kMaxValves = 4;
static constexpr uint8_t kMaxHarmonics = 16;
static constexpr uint8_t kMaxRoutes = 24;
// Six bands: a driver in a sealed chamber behind a two-stage cone has more
// than three things wrong with it, and the biquads were already there.
static constexpr uint8_t kMaxEqBands = 6;
// Saved named voicings. A/B comparison lives in the browser - it is a working
// session tool, not something the instrument has to remember.
static constexpr uint8_t kMaxVoicings = 4;
// Breakpoints of the register compensation curves.
static constexpr uint8_t kRegisterPoints = 5;
static constexpr uint8_t kNameLen = 32;

// ---------------------------------------------------------------------------
// Enumerations.  All of them are (de)serialised as strings so a configuration
// file stays readable and survives re-ordering.
// ---------------------------------------------------------------------------
enum class BoardType : uint8_t { UNKNOWN = 0, ESP32, ESP32_S3 };

enum class AudioBackendType : uint8_t {
    NONE = 0,             // silent: valves only, or safe mode
    ESP32_INTERNAL_DAC,   // prototype quality, ESP32 classic only
    MAX98357A,            // I2S class-D amp, speaker directly attached
    PCM5102A,             // I2S DAC -> external amplifier  (reference)
    ES8388,               // codec, DAC + ADC (microphone capable)
    WM8960,               // codec, DAC + ADC
    TAS5760M              // I2S class-D amp, target of the integrated PCB
};

enum class AmplifierType : uint8_t {
    NONE = 0,
    MAX98357_INTERNAL,
    TPA3118D2,
    TAS5760_INTERNAL,
    CUSTOM
};

enum class SpeakerProfileId : uint8_t {
    VISATON_FRS5_XTS = 0,
    DAYTON_CE70PR4,
    VISATON_FRS8M,
    MONACOR_SPX30M,
    CUSTOM
};

// NOTE: enumerator names avoid the Arduino core macros (OPEN, HIGH, LOW,
// DISABLED ...); the JSON spelling is kept in ConfigEnums.cpp.
enum class AcousticCouplingType : uint8_t { OPEN_AIR = 0, SEALED_CHAMBER, CUSTOM_CHAMBER };

enum class SynthEngineType : uint8_t {
    SINE = 0,
    ADDITIVE,   // reference engine for the trumpet
    WAVETABLE,
    HYBRID,     // additive + wavetable cross-fade
    BRASS_EXCITER   // EXPERIMENTAL non-linear exciter, see docs/AUDIO.md
    // SAMPLE is intentionally absent: the storage backend is not implemented
    // yet and the project rule is to never expose a non-functional option.
};

enum class ValveActuatorType : uint8_t { SERVO = 0, SOLENOID, OFF };
enum class ServoDriverType : uint8_t { ESP32_PWM = 0, PCA9685 };
enum class ValveMode : uint8_t { AUTO = 0, MANUAL, MIDI_CC, OFF };

enum class NotePriority : uint8_t { LAST = 0, HIGHEST, LOWEST };

enum class InstrumentType : uint8_t { BB_TRUMPET = 0, C_TRUMPET, EB_TRUMPET, CUSTOM };
enum class PitchInterpretation : uint8_t { CONCERT = 0, WRITTEN };

enum class WifiMode : uint8_t { AP = 0, STA, AP_STA };

enum class VibratoSource : uint8_t { CC1 = 0, AFTERTOUCH, AUTOMATIC, OFF };

enum class VelocityCurve : uint8_t { LINEAR = 0, SOFT, HARD, FIXED };

// MIDI endpoints, used by the router both as sources and as destinations.
enum class MidiPort : uint8_t {
    NONE = 0,
    USB,
    BLE,
    RTP,
    DIN,
    WEB,
    SOUND_ENGINE,   // destination only
    VALVE_ENGINE,   // destination only
    MONITOR,        // destination only (web MIDI monitor)
    COUNT
};

static constexpr uint8_t kMidiPortCount = static_cast<uint8_t>(MidiPort::COUNT);

// ---------------------------------------------------------------------------
// Sub-structures
// ---------------------------------------------------------------------------

struct I2sPins {
    int8_t bclk = 5;
    int8_t ws = 6;      // LRCK
    int8_t dout = 7;    // DIN of the DAC
    int8_t din = -1;    // codec ADC (ES8388 / WM8960), -1 when unused
    int8_t mclk = -1;   // -1 when the codec runs without MCLK
};

struct I2cPins {
    int8_t sda = 8;
    int8_t scl = 9;
    uint32_t frequency = 400000;
};

struct EqBand {
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.7f;
    bool enabled = false;
};

struct EnvelopeConfig {
    float attackMs = 12.0f;
    float decayMs = 90.0f;
    float sustain = 0.82f;     // 0..1
    float releaseMs = 70.0f;
    float attackNoise = 0.12f; // 0..1 - short chiff at note start
    float breathNoise = 0.03f; // 0..1 - continuous breath floor
};

struct VibratoConfig {
    VibratoSource source = VibratoSource::CC1;
    float frequencyHz = 5.5f;
    float depthCents = 22.0f;
    float delayMs = 250.0f;
    float fadeInMs = 350.0f;
};

struct AdditiveConfig {
    uint8_t harmonicCount = 10;
    float harmonicGain[kMaxHarmonics] = {1.00f, 0.72f, 0.55f, 0.42f, 0.33f,
                                         0.25f, 0.19f, 0.14f, 0.10f, 0.07f,
                                         0.05f, 0.04f, 0.03f, 0.02f, 0.015f,
                                         0.01f};
    // How much of the upper harmonic content follows the playing dynamics.
    float velocityBrightness = 0.85f;   // 0..1
    float breathBrightness = 0.70f;     // CC2 influence, 0..1
    float expressionBrightness = 0.35f; // CC11 influence, 0..1
    float pitchBrightness = 0.30f;      // higher notes are naturally brighter
};

struct LimiterConfig {
    bool enabled = true;
    float thresholdDb = -3.0f;
    float attackMs = 1.5f;
    float releaseMs = 90.0f;
    float hardCeiling = 0.985f;   // absolute clamp, 0..1
};

// One breakpoint of the register compensation. The driver, the chamber and
// the cone do not have a flat response, and correcting that with the master EQ
// wrecks the timbre; this corrects level and brightness as a function of the
// note instead.
struct RegisterPoint {
    uint8_t note = 60;
    float gainDb = 0.0f;
    float brightness = 0.0f;   // -1..+1, offset added to the blow amount
};

// EXPERIMENTAL. A light non-linear exciter rather than a physical model: the
// real trumpet is the resonator, so there is nothing to simulate downstream of
// the cone. Asymmetric waveshaping plus a pressure envelope is what turns a
// harmonic stack into something that behaves like a lip reed.
struct ExciterConfig {
    float drive = 1.6f;          // pre-shaper gain
    float asymmetry = 0.25f;     // -1..+1, even-harmonic content
    float pressure = 0.7f;       // 0..1, static blowing pressure
    float pressureToDrive = 0.9f;// how much the blow amount opens the shaper
    float noiseAmount = 0.05f;   // air noise fed through the shaper
    float transientMs = 18.0f;   // pressure rise at the start of a note
};

// Everything that changes the SOUND and nothing that changes SAFETY. This is
// the only structure the live preview path is allowed to touch: impedance,
// power limits, the hard ceiling, the pins and the backend all live elsewhere
// and still go through validation and a reboot.
struct VoicingConfig {
    char name[kNameLen] = "Natural";
    SynthEngineType engine = SynthEngineType::ADDITIVE;

    EnvelopeConfig envelope;
    VibratoConfig vibrato;
    AdditiveConfig additive;
    ExciterConfig exciter;

    // Spectrum shaping. These were hard-coded in AdditiveSynth: the partial
    // gains fall as harmonic^-(tilt-1), with `tilt` interpolated between the
    // dark and the bright value by the blow amount. They are exactly what has
    // to move to match an exciter to a cone.
    float darkTilt = 2.6f;
    float brightTilt = 0.6f;
    float hybridMix = 0.6f;      // 1 = all additive, 0 = all wavetable

    // Dynamics. Also previously hard-coded.
    float velocityFloor = 0.25f;        // amplitude at velocity 1
    float breathToVolume = 1.0f;        // CC2  -> level
    float expressionToVolume = 1.0f;    // CC11 -> level
    float aftertouchToBrightness = 0.25f;
    float aftertouchToVolume = 0.0f;

    uint8_t pitchBendRangeSemitones = 2;

    // Tone. The high pass is in AudioConfig because the protection stage owns
    // its floor; these are the bands on top of it.
    EqBand eq[kMaxEqBands] = {{220.0f, 0.0f, 0.8f, false},
                              {480.0f, 0.0f, 0.9f, false},
                              {900.0f, 0.0f, 0.9f, false},
                              {1800.0f, 0.0f, 0.9f, false},
                              {3200.0f, 0.0f, 0.9f, false},
                              {6400.0f, 0.0f, 0.8f, false}};

    // Register compensation, interpolated between the breakpoints.
    RegisterPoint registerCurve[kRegisterPoints] = {{52, 0.0f, 0.0f},
                                                    {60, 0.0f, 0.0f},
                                                    {67, 0.0f, 0.0f},
                                                    {72, 0.0f, 0.0f},
                                                    {86, 0.0f, 0.0f}};

    float outputTrimDb = 0.0f;   // before the limiter, never past it
};

// The named voicings the instrument remembers. Separate from the hardware
// presets on purpose: one cone, several sounds.
struct VoicingLibrary {
    uint8_t count = 0;
    VoicingConfig items[kMaxVoicings];
};

struct AudioConfig {
    AudioBackendType backend = AudioBackendType::PCM5102A;
    uint32_t sampleRate = 48000;
    uint8_t bitDepth = 24;          // requested; backend may downgrade to 16
    uint16_t blockSize = 128;       // frames per DSP block
    uint8_t dmaBuffers = 6;
    float masterVolume = 0.75f;     // 0..1
    I2sPins i2s;
    I2cPins i2c;                    // codec control bus (ES8388/WM8960/TAS5760)
    uint8_t codecAddress = 0x10;
    int8_t sdModePin = -1;          // MAX98357A SD / TAS5760 SPK_SD
    int8_t internalDacChannel = 1;  // ESP32 classic: 1 = GPIO25, 2 = GPIO26
    float highPassHz = 120.0f;
    LimiterConfig limiter;
    bool startupMute = true;
    // Program Change selects a saved voicing instead of being ignored. Off by
    // default: a sequencer that sends bank changes should not silently change
    // the sound of the instrument.
    bool programChangeSelectsVoicing = false;
};

struct AmplifierConfig {
    AmplifierType type = AmplifierType::TPA3118D2;
    float maxPowerW = 25.0f;
    float gainDb = 26.0f;
    float speakerImpedanceOhm = 8.0f;
    float volumeLimit = 1.0f;       // 0..1 hard ceiling applied to the master
};

struct SpeakerConfig {
    SpeakerProfileId profile = SpeakerProfileId::VISATON_FRS8M;
    char name[kNameLen] = "Visaton FRS 8 M";
    float impedanceOhm = 8.0f;
    float powerRmsW = 30.0f;        // manufacturer continuous rating
    float powerMaxW = 50.0f;        // manufacturer short-term maximum
    float minFrequencyHz = 100.0f;
    float maxFrequencyHz = 20000.0f;
    float recommendedHighPassHz = 160.0f;
    float gainCorrectionDb = 0.0f;
    float powerLimitW = 20.0f;      // what the protection stage enforces
    // Thiele-Small parameters, needed to predict what a sealed rear chamber
    // does to the driver's resonance.  Zero means "not entered": the acoustic
    // model then says the figure is unknown instead of inventing one.
    float fsHz = 0.0f;
    float vasLitres = 0.0f;
};

// One cone of the two-stage compression between the driver and the leadpipe.
struct HornStageConfig {
    float inletDiameterMm = 60.0f;
    float outletDiameterMm = 28.0f;
    float lengthMm = 58.0f;
};

// The coupling geometry.  See AcousticModel.h for what is derived from it and,
// just as importantly, for what is not.
struct AcousticConfig {
    AcousticCouplingType coupling = AcousticCouplingType::SEALED_CHAMBER;

    // Behind the cone.
    float rearChamberVolumeMl = 120.0f;
    // In front of the cone, before the first compression stage.
    float frontChamberVolumeMl = 35.0f;
    float frontChamberDepthMm = 8.0f;

    HornStageConfig stage1;                 // driver cone -> intermediate tube
    float intermediateDiameterMm = 28.0f;
    float intermediateLengthMm = 20.0f;
    HornStageConfig stage2{28.0f, 12.0f, 40.0f};   // intermediate -> leadpipe
    float leadpipeDiameterMm = 11.0f;

    // Bench overrides.  Zero means "not measured, derive it from the geometry
    // above"; a positive value always wins, because a measurement beats a
    // model.
    float measuredHighPassHz = 0.0f;
    float eqGainDb = 2.0f;          // gentle presence lift of the coupling
};

struct ValveConfig {
    ValveActuatorType type = ValveActuatorType::SERVO;
    // --- servo ---
    ServoDriverType driver = ServoDriverType::ESP32_PWM;
    int8_t gpio = -1;               // ESP32_PWM, or solenoid gate pin
    int8_t channel = 0;             // PCA9685 channel
    uint16_t pressedAngle = 88;
    uint16_t releasedAngle = 40;
    uint16_t speedDegPerSec = 900;
    uint16_t accelDegPerSec2 = 6000;
    bool invert = false;
    bool detachAfterMove = true;
    uint16_t detachDelayMs = 220;
    uint16_t minPulseUs = 500;
    uint16_t maxPulseUs = 2400;
    // --- solenoid ---
    bool activeHigh = true;
    uint8_t pullInPwm = 100;        // %
    uint16_t pullInMs = 50;
    uint8_t holdPwm = 35;           // %
    uint16_t maxOnMs = 5000;        // thermal guard
    uint16_t cooldownMs = 3000;
    uint8_t maxDutyPercent = 60;    // long term duty cycle guard
    // --- MIDI_CC mode ---
    uint8_t ccNumber = 20;
    // Mechanical settle time measured at the bench, in ms.  Zero means
    // "estimate it from the travel and the speed above"; a bench figure always
    // wins because the estimate ignores load, linkage slop and stiction.
    uint16_t measuredSettleMs = 0;
};

// The pistons change the resonator, so a note that starts before they have
// arrived is played through the wrong bore.  This delays the attack by the
// time the actuators actually need.
struct ValveSyncConfig {
    bool enabled = true;
    // Only wait when the fingering has to change: a repeated note on the same
    // combination needs no delay at all.
    bool onlyWhenFingeringChanges = true;
    // Added to (or subtracted from) the computed settle time, so the bench can
    // trim the result without editing every valve.
    int16_t trimMs = 0;
    // Nothing is ever delayed by more than this, whatever the arithmetic says.
    uint16_t maxDelayMs = 120;
};

struct ValvesConfig {
    uint8_t count = 3;
    ValveMode mode = ValveMode::AUTO;
    ValveConfig items[kMaxValves];
    I2cPins pca9685I2c;
    uint8_t pca9685Address = 0x40;
    int8_t pca9685OePin = -1;
    uint16_t servoFrequencyHz = 50;
    uint16_t solenoidPwmFrequencyHz = 20000;
    ValveSyncConfig sync;

    ValvesConfig() {
        // Sensible starting point: three servos on the ESP32 PWM peripheral.
        // These must not collide with the default DIN MIDI pins (17/18);
        // ConfigManager::makeDefaults() refines them per board anyway.
        items[0].gpio = 15;
        items[1].gpio = 16;
        items[2].gpio = 4;
        items[3].type = ValveActuatorType::OFF;
        items[0].ccNumber = 20;
        items[1].ccNumber = 21;
        items[2].ccNumber = 22;
        items[3].ccNumber = 23;
    }
};

// A note the user fingers differently from the standard chart. Only the
// differences are stored: the chart itself is code, and a file that repeated
// all 128 notes would fossilise a typo in the defaults for ever.
static constexpr uint8_t kNoFingeringMask = 0xFF;   // "this note has none"
static constexpr uint8_t kMaxFingeringOverrides = 48;

struct FingeringOverride {
    uint8_t written = 0;
    uint8_t primary = kNoFingeringMask;
    uint8_t alternate = kNoFingeringMask;
};

struct InstrumentConfig {
    InstrumentType type = InstrumentType::BB_TRUMPET;
    PitchInterpretation pitchMode = PitchInterpretation::CONCERT;
    int8_t customTransposeSemitones = 0;
    NotePriority notePriority = NotePriority::LAST;
    bool legato = true;
    bool retrigger = false;
    float portamentoMs = 0.0f;
    uint8_t noteMin = 52;   // E3 concert - realistic trumpet range
    uint8_t noteMax = 86;   // D6

    uint8_t fingeringOverrideCount = 0;
    FingeringOverride fingeringOverrides[kMaxFingeringOverrides];
};

struct MidiRoute {
    MidiPort source = MidiPort::NONE;
    MidiPort destination = MidiPort::NONE;
    bool enabled = false;
    uint16_t channelMask = 0xFFFF;  // bit n = MIDI channel n+1
    int8_t transpose = 0;
    VelocityCurve velocityCurve = VelocityCurve::LINEAR;
    uint8_t fixedVelocity = 100;
    uint8_t noteMin = 0;
    uint8_t noteMax = 127;
};

struct MidiDinConfig {
    bool inEnabled = true;
    bool outEnabled = true;
    bool thruEnabled = false;
    int8_t rxGpio = 18;
    int8_t txGpio = 17;
    uint8_t uartNum = 2;
};

struct MidiBleConfig {
    bool inEnabled = true;
    bool outEnabled = false;
    char deviceName[kNameLen] = "GMB MIDI Trumpet";
};

struct MidiRtpConfig {
    bool inEnabled = false;
    bool outEnabled = false;
    uint16_t controlPort = 5004;    // data port is controlPort + 1
    char sessionName[kNameLen] = "MIDI Trumpet";
};

struct MidiUsbConfig {
    bool inEnabled = true;
    bool outEnabled = false;
};

struct MidiWebConfig {
    bool inEnabled = true;
    bool monitorEnabled = true;
};

struct MidiConfig {
    MidiUsbConfig usb;
    MidiBleConfig ble;
    MidiRtpConfig rtp;
    MidiDinConfig din;
    MidiWebConfig web;
    uint16_t globalChannelMask = 0xFFFF;   // OMNI by default
    uint8_t outputChannel = 1;             // channel used when re-emitting
    bool suppressLoops = true;
    uint8_t routeCount = 0;
    MidiRoute routes[kMaxRoutes];
};

struct WifiConfig {
    WifiMode mode = WifiMode::AP;
    char ssid[kNameLen] = "";
    char password[64] = "";
    char apSsid[kNameLen] = "";      // empty -> MIDI-Trumpet-XXXX
    char apPassword[64] = "";        // empty -> open network
    char hostname[kNameLen] = "midi-trumpet";
    uint8_t apChannel = 6;
    bool captivePortal = true;
};

struct SystemConfig {
    char deviceName[kNameLen] = "Orchestrion Trumpet";
    bool safeModeForced = false;
    bool wizardCompleted = false;
    char presetName[kNameLen] = "STANDARD";
};

// ---------------------------------------------------------------------------
// Root document
// ---------------------------------------------------------------------------
struct InstrumentConfiguration {
    uint16_t schemaVersion = kConfigSchemaVersion;
    BoardType board = BoardType::UNKNOWN;
    SystemConfig system;
    WifiConfig wifi;
    AudioConfig audio;
    // The live sound, and the named voicings the instrument remembers. Kept
    // beside `audio` rather than inside it, because this is the half a preview
    // is allowed to replace at runtime and `audio` is not.
    VoicingConfig voicing;
    VoicingLibrary voicings;
    AmplifierConfig amplifier;
    SpeakerConfig speaker;
    AcousticConfig acoustic;
    ValvesConfig valves;
    InstrumentConfig instrument;
    MidiConfig midi;
};

// ---------------------------------------------------------------------------
// Enum <-> string helpers (implemented in ConfigEnums.cpp)
// ---------------------------------------------------------------------------
const char* toString(BoardType v);
const char* toString(AudioBackendType v);
const char* toString(AmplifierType v);
const char* toString(SpeakerProfileId v);
const char* toString(AcousticCouplingType v);
const char* toString(SynthEngineType v);
const char* toString(ValveActuatorType v);
const char* toString(ServoDriverType v);
const char* toString(ValveMode v);
const char* toString(NotePriority v);
const char* toString(InstrumentType v);
const char* toString(PitchInterpretation v);
const char* toString(WifiMode v);
const char* toString(VibratoSource v);
const char* toString(VelocityCurve v);
const char* toString(MidiPort v);

bool parseEnum(const char* s, BoardType& out);
bool parseEnum(const char* s, AudioBackendType& out);
bool parseEnum(const char* s, AmplifierType& out);
bool parseEnum(const char* s, SpeakerProfileId& out);
bool parseEnum(const char* s, AcousticCouplingType& out);
bool parseEnum(const char* s, SynthEngineType& out);
bool parseEnum(const char* s, ValveActuatorType& out);
bool parseEnum(const char* s, ServoDriverType& out);
bool parseEnum(const char* s, ValveMode& out);
bool parseEnum(const char* s, NotePriority& out);
bool parseEnum(const char* s, InstrumentType& out);
bool parseEnum(const char* s, PitchInterpretation& out);
bool parseEnum(const char* s, WifiMode& out);
bool parseEnum(const char* s, VibratoSource& out);
bool parseEnum(const char* s, VelocityCurve& out);
bool parseEnum(const char* s, MidiPort& out);

}  // namespace ot
