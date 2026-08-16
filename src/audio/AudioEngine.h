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
#include "audio/BrassExciter.h"
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
#include "valves/ValveTiming.h"

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
    // `valves` is used for one thing only: knowing how long the pistons need
    // before the note is worth playing.  The engine never drives them.
    void configure(const AudioConfig& audio, const VoicingConfig& voicing,
                   const SpeakerConfig& speaker, const AmplifierConfig& amplifier,
                   const AcousticConfig& acoustic, const InstrumentConfig& instrument,
                   const ValvesConfig& valves);

    // ---- live voicing ------------------------------------------------------
    // Called from the network task. The voicing is pushed into a lock-free
    // slot and picked up at the start of the next audio block, so a slider in
    // the browser is audible in about one block - and the network task never
    // touches a DSP object.
    //
    // Only the voicing can be replaced this way. Impedance, power limits, the
    // hard ceiling, the pins and the backend are not in this structure on
    // purpose: they go through validation and a reboot.
    void requestVoicing(const VoicingConfig& voicing);
    // The voicing currently sounding, whatever its provenance.
    const VoicingConfig& voicing() const { return voicing_; }
    bool voicingPending() const { return voicingPending_; }

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
    // Milliseconds the current note waited for the pistons; 0 when the note
    // started immediately.  Reported on the diagnostics page.
    uint16_t lastAttackDelayMs() const { return lastAttackDelayMs_; }
    uint8_t pitchBendRange() const { return pitchBendRange_; }
    bool sustainDown() const { return sustainDown_; }
    // Set when a Program Change asked for a voicing; the application reads it
    // and hands over the saved voicing, because the engine does not own the
    // library.
    int8_t takeProgramChange() {
        const int8_t p = programChange_;
        programChange_ = -1;
        return p;
    }
    // The engine keeps its own copy of the chart so it can tell which pistons
    // have to move for the next note.  Edits from the web UI are mirrored into
    // it, otherwise the delay would be computed from a stale table.
    FingeringEngine& fingering() { return transposer_; }
    // Pitch actually being produced, in fractional MIDI note units.
    float livePitch() const { return livePitch_; }
    float safePeakScale() const { return limiter_.peakScale(); }
    uint32_t sampleRate() const { return sampleRate_; }

private:
    void handleMessage(const MidiMessage& msg);
    void applyVoicing(const VoicingConfig& voicing);
    void takePendingVoicing();
    void releaseSustainedNotes();
    // Level and brightness correction as a function of the note, interpolated
    // between the breakpoints of the register curve.
    void registerCompensation(float note, float& gainLinear, float& brightness) const;
    // Articulation, split out so it can be deferred until the pistons arrive.
    struct PendingArticulation {
        bool active = false;
        bool articulate = false;   // false on a slur: only the pitch waits
        bool fromSilence = false;
        uint32_t samples = 0;      // remaining delay
        float heldPitch = 60.0f;   // pitch to hold until the pistons land
    };
    void startArticulation(bool fromSilence);
    void schedulePendingArticulation(size_t frames);
    uint32_t valveDelaySamples(uint8_t soundingNote);
    void updateControlRate();
    void rebuildFilters();
    float blowAmount() const;

    // ---- configuration -----------------------------------------------------
    AudioConfig cfg_;
    VoicingConfig voicing_;
    InstrumentConfig instrument_;
    SpeakerConfig speakerCfg_;
    SpeakerProfile speaker_;
    AmplifierProfile amplifier_;
    AcousticConfig acoustic_;
    ValvesConfig valves_;
    FingeringEngine transposer_;
    uint32_t sampleRate_ = 48000;

    // ---- generation --------------------------------------------------------
    NoteStack notes_;
    Envelope envelope_;
    AdditiveSynth additive_;
    WavetableSynth wavetable_;
    BrassExciter exciter_;
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
    // Fingering the pistons are currently holding, so the next note knows
    // which of them actually has to move.
    uint8_t currentValveMask_ = 0;
    PendingArticulation pending_;
    uint16_t controlCounter_ = 0;
    float peakLevel_ = 0.0f;
    float masterVolume_ = 0.75f;
    float acousticGainLinear_ = 1.0f;
    float outputTrimLinear_ = 1.0f;
    float registerGain_ = 1.0f;
    float registerBrightness_ = 0.0f;

    // ---- controllers -------------------------------------------------------
    // Sustain: CC64 holds the note after the key is released, exactly like a
    // piano pedal. The note stack is not touched until the pedal comes up, so
    // the valves stay down too - which is what a sequencer expects.
    bool sustainDown_ = false;
    bool sustainedNote_ = false;
    // RPN 0 (pitch bend sensitivity). Parsed from CC101/100/6/38 so a
    // sequencer that sets its own bend range is obeyed.
    uint8_t rpnMsb_ = 0x7F;
    uint8_t rpnLsb_ = 0x7F;
    uint8_t pitchBendRange_ = 2;
    uint8_t ccModulation_ = 0;
    uint8_t ccBreath_ = 0;
    uint8_t ccVolume_ = 100;
    uint8_t ccExpression_ = 127;
    uint8_t channelPressure_ = 0;
    int16_t pitchBendRaw_ = 0;
    uint8_t currentVelocity_ = 0;

    uint16_t lastAttackDelayMs_ = 0;
    int8_t programChange_ = -1;
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

    // Single-slot mailbox for the live voicing. A ring is pointless here: only
    // the newest setting matters, and a slider produces far more updates than
    // there are blocks.
    VoicingConfig pendingVoicing_;
    volatile bool voicingPending_ = false;
};

}  // namespace ot
