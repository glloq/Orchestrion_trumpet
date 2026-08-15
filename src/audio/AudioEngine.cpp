#include "audio/AudioEngine.h"

#include <cmath>

namespace ot {

namespace {
// Pitch, brightness and filter parameters only need to move at control rate.
// 32 samples at 48 kHz is 1.5 kHz, far above anything a player can hear as a
// staircase, and it keeps the per-sample loop free of pow()/exp().
constexpr uint16_t kControlDivider = 32;
}  // namespace

void AudioEngine::configure(const AudioConfig& audio, const SpeakerConfig& speaker,
                            const AmplifierConfig& amplifier, const AcousticConfig& acoustic,
                            const InstrumentConfig& instrument) {
    cfg_ = audio;
    instrument_ = instrument;
    acoustic_ = acoustic;
    sampleRate_ = audio.sampleRate ? audio.sampleRate : 48000;

    speaker_.configure(speaker);
    amplifier_.configure(amplifier);
    transposer_.configure(instrument);

    initSineTable();
    notes_.setPriority(instrument.notePriority);
    envelope_.configure(cfg_.envelope, sampleRate_);
    AdditiveConfig additiveCfg = cfg_.additive;
    if (cfg_.engine == SynthEngineType::SINE) additiveCfg.harmonicCount = 1;
    additive_.configure(additiveCfg, sampleRate_);
    wavetable_.configure(cfg_.additive, sampleRate_);
    // The LFO is advanced once per control block, not once per sample, so it
    // must run on the control rate clock.  Handing it the audio sample rate
    // would divide the vibrato frequency by kControlDivider - a 5.5 Hz setting
    // would come out at 0.17 Hz.
    vibratoLfo_.setSampleRate(sampleRate_ / kControlDivider);
    vibratoLfo_.setFrequency(cfg_.vibrato.frequencyHz);
    testOsc_.setSampleRate(sampleRate_);

    limiter_.configure(cfg_.limiter, sampleRate_);
    limiter_.setPeakScale(SpeakerProtection::safePeakScale(speaker_, amplifier_));
    dcBlocker_.setSampleRate(sampleRate_);

    masterVolume_ = clampValue(cfg_.masterVolume, 0.0f, 1.0f);
    acousticGainLinear_ = std::pow(10.0f, acousticGainDb(acoustic_) / 20.0f);

    vibratoDelaySamples_ =
        static_cast<uint32_t>(cfg_.vibrato.delayMs * 0.001f * static_cast<float>(sampleRate_));
    vibratoFadeSamples_ =
        static_cast<uint32_t>(cfg_.vibrato.fadeInMs * 0.001f * static_cast<float>(sampleRate_));
    if (vibratoFadeSamples_ == 0) vibratoFadeSamples_ = 1;

    if (instrument_.portamentoMs > 0.5f) {
        const float samples = instrument_.portamentoMs * 0.001f * static_cast<float>(sampleRate_) /
                              static_cast<float>(kControlDivider);
        portamentoCoef_ = std::exp(-3.0f / (samples < 1.0f ? 1.0f : samples));
    } else {
        portamentoCoef_ = 0.0f;
    }

    rebuildFilters();
}

void AudioEngine::rebuildFilters() {
    const float hpf = SpeakerProtection::effectiveHighPassHz(
        cfg_.highPassHz, speaker_.recommendedHighPassHz(), acousticHighPassHz(acoustic_));
    highPass_.setHighPass(hpf, 0.707f, sampleRate_);

    for (uint8_t i = 0; i < kMaxEqBands; ++i) {
        if (cfg_.eq[i].enabled) {
            userEq_[i].setPeaking(cfg_.eq[i].frequency, cfg_.eq[i].gainDb, cfg_.eq[i].q,
                                  sampleRate_);
        } else {
            userEq_[i].setBypass(true);
        }
        const EqBand& band = speaker_.eqBand(i);
        if (band.enabled) {
            speakerEq_[i].setPeaking(band.frequency, band.gainDb + speaker_.gainCorrectionDb(),
                                     band.q, sampleRate_);
        } else {
            speakerEq_[i].setBypass(true);
        }
    }
}

bool AudioEngine::begin() {
    started_ = true;
    muted_ = cfg_.startupMute;
    envelope_.reset();
    notes_.clear();
    limiter_.reset();
    dcBlocker_.reset();
    highPass_.reset();
    for (uint8_t i = 0; i < kMaxEqBands; ++i) {
        userEq_[i].reset();
        speakerEq_[i].reset();
    }
    return true;
}

void AudioEngine::setMasterVolume(float volume) {
    masterVolume_ = clampValue(volume, 0.0f, 1.0f);
}

void AudioEngine::onMidi(const MidiMessage& msg) {
    // MIDI task side: never touch the DSP state directly.
    queue_.push(msg);
}

void AudioEngine::handleMessage(const MidiMessage& msg) {
    switch (msg.type) {
        case MidiType::NoteOn:
        case MidiType::NoteOff: {
            const bool wasSounding = notes_.hasNote();
            bool changed;
            if (msg.isNoteOn()) {
                changed = notes_.noteOn(msg.data1, msg.data2);
            } else {
                changed = notes_.noteOff(msg.data1);
            }

            if (!notes_.hasNote()) {
                envelope_.noteOff();
                break;
            }
            if (!changed && wasSounding) break;   // a held lower note stays

            currentVelocity_ = notes_.activeVelocity();
            targetPitch_ = static_cast<float>(transposer_.soundingNote(notes_.activeNote()));

            // A note starting from silence always restarts the envelope.  While
            // notes overlap, `legato` decides between gliding into the new
            // pitch and re-articulating it.
            const bool fromSilence = !wasSounding;
            const bool articulate = fromSilence || !instrument_.legato || instrument_.retrigger;

            if (fromSilence || portamentoCoef_ == 0.0f) currentPitch_ = targetPitch_;

            if (articulate) {
                envelope_.noteOn(fromSilence || instrument_.retrigger);
                if (fromSilence || instrument_.retrigger) {
                    additive_.resetPhase();
                    wavetable_.resetPhase();
                }
                noteAgeSamples_ = 0;
            }
            break;
        }

        case MidiType::ControlChange:
            switch (msg.data1) {
                case cc::Modulation:
                    ccModulation_ = msg.data2;
                    break;
                case cc::Breath:
                    ccBreath_ = msg.data2;
                    break;
                case cc::Volume:
                    ccVolume_ = msg.data2;
                    break;
                case cc::Expression:
                    ccExpression_ = msg.data2;
                    break;
                case cc::AllSoundOff:
                    notes_.clear();
                    envelope_.reset();
                    break;
                case cc::AllNotesOff:
                    notes_.clear();
                    envelope_.noteOff();
                    break;
                case cc::ResetControllers:
                    ccModulation_ = 0;
                    ccBreath_ = 0;
                    ccVolume_ = 100;
                    ccExpression_ = 127;
                    channelPressure_ = 0;
                    pitchBendRaw_ = 0;
                    break;
                default:
                    break;
            }
            break;

        case MidiType::PitchBend:
            pitchBendRaw_ = msg.pitchBendValue();
            break;

        case MidiType::ChannelPressure:
            channelPressure_ = msg.data1;
            break;

        case MidiType::SystemReset:
            notes_.clear();
            envelope_.reset();
            break;

        default:
            // Program Change, SysEx and MPE are accepted by the router and
            // simply ignored here until the corresponding feature exists.
            break;
    }
}

float AudioEngine::blowAmount() const {
    // Velocity is the baseline; CC2 (breath) and CC11 (expression) push the
    // instrument brighter exactly like a stronger air column would.
    const float vel = static_cast<float>(currentVelocity_) / 127.0f;
    const float breath = static_cast<float>(ccBreath_) / 127.0f;
    const float expr = static_cast<float>(ccExpression_) / 127.0f;
    const float pressure = static_cast<float>(channelPressure_) / 127.0f;

    float blow = vel * cfg_.additive.velocityBrightness;
    blow += breath * cfg_.additive.breathBrightness;
    blow += (expr - 0.5f) * cfg_.additive.expressionBrightness;
    blow += pressure * 0.25f;
    const float weight = cfg_.additive.velocityBrightness + cfg_.additive.breathBrightness + 0.25f;
    return clampValue(weight > 0.01f ? blow / weight : vel, 0.0f, 1.0f);
}

void AudioEngine::updateControlRate() {
    // ---- pitch ------------------------------------------------------------
    if (portamentoCoef_ > 0.0f) {
        currentPitch_ = targetPitch_ + (currentPitch_ - targetPitch_) * portamentoCoef_;
    } else {
        currentPitch_ = targetPitch_;
    }

    const float bendSemitones = (static_cast<float>(pitchBendRaw_) / 8192.0f) *
                                static_cast<float>(cfg_.pitchBendRangeSemitones);

    // ---- vibrato ----------------------------------------------------------
    float vibratoDepth = 0.0f;
    switch (cfg_.vibrato.source) {
        case VibratoSource::CC1:
            vibratoDepth = static_cast<float>(ccModulation_) / 127.0f;
            break;
        case VibratoSource::AFTERTOUCH:
            vibratoDepth = static_cast<float>(channelPressure_) / 127.0f;
            break;
        case VibratoSource::AUTOMATIC:
            vibratoDepth = 1.0f;
            break;
        case VibratoSource::OFF:
        default:
            vibratoDepth = 0.0f;
            break;
    }
    float fade = 0.0f;
    if (noteAgeSamples_ > vibratoDelaySamples_) {
        fade = static_cast<float>(noteAgeSamples_ - vibratoDelaySamples_) /
               static_cast<float>(vibratoFadeSamples_);
        if (fade > 1.0f) fade = 1.0f;
    }
    vibratoPhaseGain_ = vibratoDepth * fade;

    const float vibrato = vibratoLfo_.next() * vibratoPhaseGain_ *
                          (cfg_.vibrato.depthCents / 100.0f);

    const float pitch = currentPitch_ + bendSemitones + vibrato;
    const float frequency = noteToFrequency(pitch);
    // Reported as the "current frequency": what is really being produced,
    // pitch bend and vibrato included, not just the note that was pressed.
    livePitch_ = pitch;

    // ---- timbre -----------------------------------------------------------
    const float blow = blowAmount();
    const float pitchNorm = clampValue((pitch - 40.0f) / 50.0f, 0.0f, 1.0f);

    switch (cfg_.engine) {
        case SynthEngineType::SINE:
            additive_.setFrequency(frequency);
            break;
        case SynthEngineType::WAVETABLE:
            wavetable_.setFrequency(frequency);
            wavetable_.setBrightness(blow);
            break;
        case SynthEngineType::HYBRID:
            additive_.setFrequency(frequency);
            additive_.setBrightness(blow);
            additive_.setPitchNormalised(pitchNorm);
            wavetable_.setFrequency(frequency);
            wavetable_.setBrightness(blow);
            break;
        case SynthEngineType::ADDITIVE:
        default:
            additive_.setFrequency(frequency);
            additive_.setBrightness(blow);
            additive_.setPitchNormalised(pitchNorm);
            break;
    }

    // ---- amplitude --------------------------------------------------------
    const float vel = static_cast<float>(currentVelocity_) / 127.0f;
    const float vol = static_cast<float>(ccVolume_) / 127.0f;
    const float expr = static_cast<float>(ccExpression_) / 127.0f;
    // CC2 acts as a continuous air supply: when it is used it takes over from
    // the note velocity, which is what a breath controller player expects.
    const float breathAmp = ccBreath_ > 0 ? static_cast<float>(ccBreath_) / 127.0f : 1.0f;
    float amplitude = (0.25f + 0.75f * vel) * vol * expr * breathAmp;
    smoothedAmplitude_ += (amplitude - smoothedAmplitude_) * 0.25f;
}

void AudioEngine::renderBlock(float* out, size_t frames) {
    // Drain the MIDI queue at block boundaries: bounded work, no locking.
    MidiMessage msg;
    uint8_t guard = 0;
    while (guard++ < 32 && queue_.pop(msg)) handleMessage(msg);

    if (!started_) {
        for (size_t i = 0; i < frames; ++i) out[i] = 0.0f;
        return;
    }

    float peak = 0.0f;

    for (size_t i = 0; i < frames; ++i) {
        if (controlCounter_ == 0) updateControlRate();
        controlCounter_ = static_cast<uint16_t>((controlCounter_ + 1) % kControlDivider);
        ++noteAgeSamples_;

        float s;
        if (testSignal_ != TestSignal::NONE) {
            if (testSignal_ == TestSignal::SWEEP && testTotalSamples_ > 0) {
                const float t = 1.0f - static_cast<float>(testRemainingSamples_) /
                                           static_cast<float>(testTotalSamples_);
                // Logarithmic sweep: equal time per octave.
                const float f = testStartHz_ * std::pow(testEndHz_ / testStartHz_, t);
                testOsc_.setFrequency(f);
            }
            s = testOsc_.next() * testAmplitude_;
            if (testRemainingSamples_ > 0 && --testRemainingSamples_ == 0) {
                testSignal_ = TestSignal::NONE;
            }
        } else {
            const float env = envelope_.process();
            switch (cfg_.engine) {
                case SynthEngineType::SINE:
                    // Configured with a single harmonic, so the additive
                    // generator IS the sine generator here.
                    s = additive_.process();
                    break;
                case SynthEngineType::WAVETABLE:
                    s = wavetable_.process();
                    break;
                case SynthEngineType::HYBRID: {
                    const float a = additive_.process();
                    const float w = wavetable_.process();
                    s = a * 0.6f + w * 0.4f;
                    break;
                }
                case SynthEngineType::ADDITIVE:
                default:
                    s = additive_.process();
                    break;
            }
            s *= env;
            // Breath floor plus the short chiff at the very beginning of a
            // note: two low level noise components, they cost one xorshift.
            const float n = noise_.next();
            s += n * (cfg_.envelope.breathNoise * env +
                      cfg_.envelope.attackNoise * envelope_.attackTransient());
            s *= smoothedAmplitude_;
        }

        s = highPass_.process(s);
        for (uint8_t b = 0; b < kMaxEqBands; ++b) s = userEq_[b].process(s);
        for (uint8_t b = 0; b < kMaxEqBands; ++b) s = speakerEq_[b].process(s);
        s *= acousticGainLinear_;
        s = dcBlocker_.process(s);
        dcBlocker_.observe(s);
        s *= masterVolume_;
        s = limiter_.process(s);

        if (muted_) s = 0.0f;

        const float a = s < 0.0f ? -s : s;
        if (a > peak) peak = a;
        out[i] = s;
    }

    // Cheap decaying peak meter for the dashboard.
    peakLevel_ = peak > peakLevel_ ? peak : peakLevel_ * 0.85f + peak * 0.15f;
}

void AudioEngine::allNotesOff() {
    MidiMessage m = MidiMessage::controlChange(1, cc::AllNotesOff, 0);
    queue_.push(m);
}

void AudioEngine::panic() {
    // Panic runs from whatever task asked for it, so it goes through the same
    // queue as everything else; the audio task applies it on the next block,
    // at most a couple of milliseconds later.
    muted_ = true;
    MidiMessage m = MidiMessage::controlChange(1, cc::AllSoundOff, 0);
    queue_.push(m);
    testSignal_ = TestSignal::NONE;
}

void AudioEngine::startTestTone(float frequencyHz, float amplitude, uint32_t durationMs) {
    testAmplitude_ = clampValue(amplitude, 0.0f, 0.8f);
    testOsc_.setFrequency(clampValue(frequencyHz, 20.0f, 18000.0f));
    testTotalSamples_ =
        static_cast<uint32_t>(static_cast<float>(durationMs) * 0.001f * static_cast<float>(sampleRate_));
    testRemainingSamples_ = testTotalSamples_;
    testSignal_ = TestSignal::TONE;
}

void AudioEngine::startSweep(float startHz, float endHz, uint32_t durationMs, float amplitude) {
    testAmplitude_ = clampValue(amplitude, 0.0f, 0.8f);
    testStartHz_ = clampValue(startHz, 20.0f, 18000.0f);
    testEndHz_ = clampValue(endHz, 20.0f, 18000.0f);
    testTotalSamples_ =
        static_cast<uint32_t>(static_cast<float>(durationMs) * 0.001f * static_cast<float>(sampleRate_));
    if (testTotalSamples_ == 0) testTotalSamples_ = sampleRate_;
    testRemainingSamples_ = testTotalSamples_;
    testOsc_.setFrequency(testStartHz_);
    testSignal_ = TestSignal::SWEEP;
}

void AudioEngine::stopTestSignal() {
    testSignal_ = TestSignal::NONE;
    testRemainingSamples_ = 0;
}

AudioEngineStatus AudioEngine::status() const {
    AudioEngineStatus s;
    s.noteActive = notes_.hasNote();
    s.note = notes_.hasNote() ? notes_.activeNote() : 0;
    s.velocity = currentVelocity_;
    s.frequencyHz =
        notes_.hasNote() || envelope_.isActive() ? noteToFrequency(livePitch_) : 0.0f;
    s.envelope = envelope_.value();
    s.peakLevel = peakLevel_;
    s.gainReduction = limiter_.gainReduction();
    s.clipEvents = limiter_.clipEvents();
    s.droppedMessages = queue_.dropped();
    s.modulation = ccModulation_;
    s.breath = ccBreath_;
    s.volume = ccVolume_;
    s.expression = ccExpression_;
    s.pitchBend = pitchBendRaw_;
    return s;
}

}  // namespace ot
