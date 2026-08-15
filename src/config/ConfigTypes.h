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
static constexpr uint16_t kConfigSchemaVersion = 2;

static constexpr uint8_t kMaxValves = 4;
static constexpr uint8_t kMaxHarmonics = 16;
static constexpr uint8_t kMaxRoutes = 24;
static constexpr uint8_t kMaxEqBands = 3;
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
    HYBRID      // additive + wavetable cross-fade
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

struct AudioConfig {
    AudioBackendType backend = AudioBackendType::PCM5102A;
    uint32_t sampleRate = 48000;
    uint8_t bitDepth = 24;          // requested; backend may downgrade to 16
    uint16_t blockSize = 128;       // frames per DSP block
    uint8_t dmaBuffers = 6;
    float masterVolume = 0.75f;     // 0..1
    SynthEngineType engine = SynthEngineType::ADDITIVE;
    uint8_t pitchBendRangeSemitones = 2;
    I2sPins i2s;
    I2cPins i2c;                    // codec control bus (ES8388/WM8960/TAS5760)
    uint8_t codecAddress = 0x10;
    int8_t sdModePin = -1;          // MAX98357A SD / TAS5760 SPK_SD
    int8_t internalDacChannel = 1;  // ESP32 classic: 1 = GPIO25, 2 = GPIO26
    float highPassHz = 120.0f;
    EqBand eq[kMaxEqBands] = {{220.0f, 0.0f, 0.8f, false},
                              {900.0f, 0.0f, 0.9f, false},
                              {3200.0f, 0.0f, 0.9f, false}};
    EnvelopeConfig envelope;
    VibratoConfig vibrato;
    AdditiveConfig additive;
    LimiterConfig limiter;
    bool startupMute = true;
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
    float powerRmsW = 30.0f;
    float minFrequencyHz = 150.0f;
    float maxFrequencyHz = 20000.0f;
    float recommendedHighPassHz = 160.0f;
    float gainCorrectionDb = 0.0f;
    float powerLimitW = 20.0f;      // what the protection stage enforces
};

struct AcousticConfig {
    AcousticCouplingType coupling = AcousticCouplingType::SEALED_CHAMBER;
    float chamberVolumeMl = 120.0f;
    float outletDiameterMm = 11.0f;
    float outletLengthMm = 45.0f;
    float highPassHz = 170.0f;
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
