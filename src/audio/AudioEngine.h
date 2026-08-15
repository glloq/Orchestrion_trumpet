// ============================================================================
//  AudioEngine.h - the sound engine.
//
//        MIDI note -> pitch generator -> harmonic generator -> envelope
//               -> attack noise -> vibrato -> EQ -> speaker compensation
//               -> limiter -> audio backend
//
//  The engine knows nothing about I2S, the DAC or the amplifier: it produces
//  a mono float stream and the backend deals with the wire format.
//
//  Threading: onMidi() is called from the MIDI task and only pushes into a
//  lock free queue.  renderBlock() drains it from the audio task.  No mutex is
//  taken anywhere in this file.
// ============================================================================
#pragma once

#include "audio/AdditiveSynth.h"
#include "audio/Envelope.h"
#include "audio/Filters.h"
#include "audio/IAudioEngine.h"
#include "audio/Limiter.h"
#include "audio/Oscillator.h"
#include "audio/Profiles.h"
#include "audio/WavetableSynth.h"
#include "core/RingBuffer.h"
#include "midi/IMidiTransport.h"
#include "midi/NoteStack.h"
#include "valves/FingeringEngine.h"

namespace ot {

enum class TestSignal : uint8_t { NONE = 0, TONE, SWEEP };

struct AudioEngineStatus {
    bool noteActive = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    float frequencyHz = 0.0f;
    float envelope = 0.0f;
    float peakLevel = 0.0f;
    float gainReduction = 1.0f;
    uint32_t clipEvents = 0;
    uint32_t droppedMessages = 0;
    uint8_t modulation = 0;
    uint8_t breath = 0;
    uint8_t volume = 100;
    uint8_t expression = 127;
    int16_t pitchBend = 0;
};

class AudioEngine final : public IAudioEngine, public IMidiSink {
public:
    void configure(const AudioConfig& audio, const SpeakerConfig& speaker,
                   const AmplifierConfig& amplifier, const AcousticConfig& acoustic,
                   const InstrumentConfig& instrument);

    bool begin() override;
    void renderBlock(float* out, size_t frames) override;
    void allNotesOff() override;
    void panic() override;
    void setMasterVolume(float volume) override;
    float masterVolume() const override { return masterVolume_; }

    // Called from the MIDI task.
    void onMidi(const MidiMessage& msg) override;

    void setMuted(bool muted) { muted_ = muted; }
    bool isMuted() const { return muted_; }

    // Audio test / calibration page.
    void startTestTone(float frequencyHz, float amplitude, uint32_t durationMs);
    void startSweep(float startHz, float endHz, uint32_t durationMs, float amplitude);
    void stopTestSignal();
    bool testSignalActive() const { return testSignal_ != TestSignal::NONE; }

    AudioEngineStatus status() const;
    // Pitch actually being produced, in fractional MIDI note units.
    float livePitch() const { return livePitch_; }
    float safePeakScale() const { return limiter_.peakScale(); }
    uint32_t sampleRate() const { return sampleRate_; }

private:
    void handleMessage(const MidiMessage& msg);
    void updateControlRate();
    void rebuildFilters();
    float blowAmount() const;

    // ---- configuration -----------------------------------------------------
    AudioConfig cfg_;
    InstrumentConfig instrument_;
    SpeakerProfile speaker_;
    AmplifierProfile amplifier_;
    AcousticConfig acoustic_;
    FingeringEngine transposer_;
    uint32_t sampleRate_ = 48000;

    // ---- generation --------------------------------------------------------
    NoteStack notes_;
    Envelope envelope_;
    AdditiveSynth additive_;
    WavetableSynth wavetable_;
    NoiseSource noise_;
    Oscillator vibratoLfo_;

    Biquad highPass_;
    Biquad userEq_[kMaxEqBands];
    Biquad speakerEq_[kMaxEqBands];
    DcBlocker dcBlocker_;
    Limiter limiter_;

    // ---- live state (audio task) -------------------------------------------
    float currentPitch_ = 60.0f;   // in MIDI note units, fractional
    float livePitch_ = 60.0f;      // currentPitch_ + bend + vibrato
    float targetPitch_ = 60.0f;
    float portamentoCoef_ = 0.0f;
    float smoothedAmplitude_ = 0.0f;
    float vibratoPhaseGain_ = 0.0f;
    uint32_t vibratoDelaySamples_ = 0;
    uint32_t vibratoFadeSamples_ = 0;
    uint32_t noteAgeSamples_ = 0;
    uint16_t controlCounter_ = 0;
    float peakLevel_ = 0.0f;
    float masterVolume_ = 0.75f;
    float acousticGainLinear_ = 1.0f;

    // ---- controllers -------------------------------------------------------
    uint8_t ccModulation_ = 0;
    uint8_t ccBreath_ = 0;
    uint8_t ccVolume_ = 100;
    uint8_t ccExpression_ = 127;
    uint8_t channelPressure_ = 0;
    int16_t pitchBendRaw_ = 0;
    uint8_t currentVelocity_ = 0;

    bool muted_ = true;      // boots muted, unmuted at the end of the sequence
    bool started_ = false;

    // ---- test signal -------------------------------------------------------
    TestSignal testSignal_ = TestSignal::NONE;
    Oscillator testOsc_;
    float testAmplitude_ = 0.2f;
    float testStartHz_ = 100.0f;
    float testEndHz_ = 5000.0f;
    uint32_t testRemainingSamples_ = 0;
    uint32_t testTotalSamples_ = 0;

    RingBuffer<MidiMessage, 64> queue_;
};

}  // namespace ot
