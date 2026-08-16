#include "audio/AudioEngine.h"

#include <cmath>

namespace ot {

namespace {
// Pitch, brightness and filter parameters only need to move at control rate.
// 32 samples at 48 kHz is 1.5 kHz, far above anything a player can hear as a
// staircase, and it keeps the per-sample loop free of pow()/exp().
constexpr uint16_t kControlDivider = 32;
}  // namespace

void AudioEngine::configure(const AudioConfig& audio, const VoicingConfig& voicing,
                            const SpeakerConfig& speaker, const AmplifierConfig& amplifier,
                            const AcousticConfig& acoustic, const InstrumentConfig& instrument,
                            const ValvesConfig& valves) {
    cfg_ = audio;
    instrument_ = instrument;
    acoustic_ = acoustic;
    valves_ = valves;
    speakerCfg_ = speaker;
    sampleRate_ = audio.sampleRate ? audio.sampleRate : 48000;

    speaker_.configure(speaker);
    amplifier_.configure(amplifier);
    transposer_.configure(instrument);

    initSineTable();
    notes_.setPriority(instrument.notePriority);
    testOsc_.setSampleRate(sampleRate_);

    limiter_.configure(cfg_.limiter, sampleRate_);
    limiter_.setPeakScale(SpeakerProtection::safePeakScale(speaker_, amplifier_));
    dcBlocker_.setSampleRate(sampleRate_);

    masterVolume_ = clampValue(cfg_.masterVolume, 0.0f, 1.0f);
    acousticGainLinear_ = std::pow(10.0f, acousticGainDb(acoustic_) / 20.0f);

    if (instrument_.portamentoMs > 0.5f) {
        const float samples = instrument_.portamentoMs * 0.001f * static_cast<float>(sampleRate_) /
                              static_cast<float>(kControlDivider);
        portamentoCoef_ = std::exp(-3.0f / (samples < 1.0f ? 1.0f : samples));
    } else {
        portamentoCoef_ = 0.0f;
    }

    // Boot path, on the application task: build the tables outright. From here
    // on they are only ever rebuilt by requestVoicing(), off the audio task.
    wavetable_.configure(voicing.additive, sampleRate_, voicing.darkTilt, voicing.brightTilt);
    applyVoicing(voicing);
}

// Everything a live preview is allowed to change, and nothing else.  Called
// both from configure() and, at a block boundary, from takePendingVoicing():
// one code path, so a previewed sound is exactly the sound a reboot gives.
void AudioEngine::applyVoicing(const VoicingConfig& voicing) {
    voicing_ = voicing;

    envelope_.configure(voicing_.envelope, sampleRate_);
    AdditiveConfig additiveCfg = voicing_.additive;
    if (voicing_.engine == SynthEngineType::SINE) additiveCfg.harmonicCount = 1;
    additive_.configure(additiveCfg, sampleRate_);
    additive_.setTilt(voicing_.darkTilt, voicing_.brightTilt);
    // The wavetable is NOT rebuilt here: this runs on the audio task at a block
    // boundary and a rebuild is a hundred thousand sin() calls. prepareVoicing()
    // has already done the work on the calling task; all that is left is to
    // take it, which is one compare-and-swap.
    if (!wavetable_.adopt() && !wavetable_.matches(voicing_.additive, sampleRate_,
                                                   voicing_.darkTilt, voicing_.brightTilt)) {
        ++wavetableMisses_;
    }
    exciter_.configure(voicing_.exciter, sampleRate_);

    // The LFO is advanced once per control block, not once per sample, so it
    // must run on the control rate clock.  Handing it the audio sample rate
    // would divide the vibrato frequency by kControlDivider - a 5.5 Hz setting
    // would come out at 0.17 Hz.
    vibratoLfo_.setSampleRate(sampleRate_ / kControlDivider);
    vibratoLfo_.setFrequency(voicing_.vibrato.frequencyHz);

    vibratoDelaySamples_ =
        static_cast<uint32_t>(voicing_.vibrato.delayMs * 0.001f * static_cast<float>(sampleRate_));
    vibratoFadeSamples_ =
        static_cast<uint32_t>(voicing_.vibrato.fadeInMs * 0.001f * static_cast<float>(sampleRate_));
    if (vibratoFadeSamples_ == 0) vibratoFadeSamples_ = 1;

    // A voicing carries its own bend range; a sequencer can still override it
    // at runtime with RPN 0.
    pitchBendRange_ = voicing_.pitchBendRangeSemitones ? voicing_.pitchBendRangeSemitones : 2;
    outputTrimLinear_ = std::pow(10.0f, clampValue(voicing_.outputTrimDb, -24.0f, 12.0f) / 20.0f);

    rebuildFilters();
}

// Called from the network task (a preview) or the application task (a Program
// Change).  Those two are serialised by AppController; the audio task is not
// involved and never waits.
//
// Everything expensive about a voicing change happens right here, on the
// caller's task, before the audio task is told anything: the mailbox only ever
// carries a value that is ready to be used.
void AudioEngine::requestVoicing(const VoicingConfig& voicing) {
    if (!wavetable_.matches(voicing.additive, sampleRate_, voicing.darkTilt,
                            voicing.brightTilt)) {
        // Fails only while a previous build is still waiting to be taken, which
        // lasts at most one audio block. Retrying costs this task a couple of
        // milliseconds and costs the audio task nothing.
        for (uint8_t attempt = 0; attempt < 16; ++attempt) {
            if (wavetable_.prepare(voicing.additive, sampleRate_, voicing.darkTilt,
                                   voicing.brightTilt)) {
                break;
            }
            sleepMs(2);
        }
    }
    voicingMailbox_.publish(voicing);
}

void AudioEngine::takePendingVoicing() {
    VoicingConfig next;
    if (!voicingMailbox_.take(next)) return;
    // The envelope is deliberately left alone: moving a slider must not
    // retrigger the note being auditioned, or a live preview is useless.
    applyVoicing(next);
}

void AudioEngine::rebuildFilters() {
    const float hpf = SpeakerProtection::effectiveHighPassHz(
        cfg_.highPassHz, speaker_.recommendedHighPassHz(),
        acousticHighPassHz(acoustic_, speakerCfg_));
    highPass_.setHighPass(hpf, 0.707f, sampleRate_);

    for (uint8_t i = 0; i < kMaxEqBands; ++i) {
        if (voicing_.eq[i].enabled) {
            userEq_[i].setPeaking(voicing_.eq[i].frequency, voicing_.eq[i].gainDb,
                                  voicing_.eq[i].q, sampleRate_);
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
                if (sustainDown_) {
                    // The pedal is down: the key came up but the note has not.
                    // Nothing is released and the valves stay where they are.
                    sustainedNote_ = true;
                    break;
                }
                pending_.active = false;      // nothing left to articulate
                lastAttackDelayMs_ = 0;
                envelope_.noteOff();
                currentValveMask_ = 0;
                break;
            }
            sustainedNote_ = false;
            if (!changed && wasSounding) break;   // a held lower note stays

            currentVelocity_ = notes_.activeVelocity();
            targetPitch_ = static_cast<float>(transposer_.soundingNote(notes_.activeNote()));

            // A note starting from silence always restarts the envelope.  While
            // notes overlap, `legato` decides between gliding into the new
            // pitch and re-articulating it.
            const bool fromSilence = !wasSounding;
            const bool articulate = fromSilence || !instrument_.legato || instrument_.retrigger;

            // Wait for the pistons.  The valve engine received the same
            // Note-On at the same instant, but its actuators are mechanical:
            // starting the attack now would sound the first tens of
            // milliseconds through the previous fingering.
            const uint32_t delay = valveDelaySamples(notes_.activeNote());

            if (delay > 0) {
                // Either way the pitch waits for the pistons. A slurred note
                // does not re-articulate, but it still must not sound through
                // the previous fingering: gliding to the new pitch while the
                // valves are still moving plays it through the wrong bore just
                // as surely as a fresh attack would.
                pending_.active = true;
                pending_.articulate = articulate;
                pending_.fromSilence = fromSilence;
                pending_.samples = delay;
                pending_.heldPitch = currentPitch_;
                currentPitch_ = pending_.heldPitch;   // freeze until it lands
            } else {
                if (fromSilence || portamentoCoef_ == 0.0f) currentPitch_ = targetPitch_;
                if (articulate) startArticulation(fromSilence);
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
                case cc::Sustain:
                    // CC64: >= 64 is down. Releasing the pedal releases a note
                    // whose key has already come up.
                    sustainDown_ = msg.data2 >= 64;
                    if (!sustainDown_) releaseSustainedNotes();
                    break;
                case cc::RpnMsb:
                    rpnMsb_ = msg.data2;
                    break;
                case cc::RpnLsb:
                    rpnLsb_ = msg.data2;
                    break;
                case cc::DataEntryMsb:
                    // RPN 0 is pitch bend sensitivity, in semitones. Anything
                    // else is not implemented and is deliberately ignored
                    // rather than silently mapped onto something.
                    if (rpnMsb_ == 0 && rpnLsb_ == 0 && msg.data2 > 0 && msg.data2 <= 48) {
                        pitchBendRange_ = msg.data2;
                    }
                    break;
                case cc::AllSoundOff:
                    sustainDown_ = false;
                    sustainedNote_ = false;
                    pending_.active = false;
                    lastAttackDelayMs_ = 0;
                    currentValveMask_ = 0;
                    notes_.clear();
                    envelope_.reset();
                    break;
                case cc::AllNotesOff:
                    sustainDown_ = false;
                    sustainedNote_ = false;
                    pending_.active = false;
                    lastAttackDelayMs_ = 0;
                    currentValveMask_ = 0;
                    notes_.clear();
                    envelope_.noteOff();
                    break;
                case cc::ResetControllers:
                    sustainDown_ = false;
                    releaseSustainedNotes();
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

        case MidiType::ProgramChange:
            // Latched for the application, which owns the voicing library.
            // Ignored entirely unless the user turned the option on.
            if (cfg_.programChangeSelectsVoicing) {
                programChange_ = static_cast<int8_t>(msg.data1 & 0x7F);
            }
            break;

        case MidiType::SystemReset:
            sustainDown_ = false;
            sustainedNote_ = false;
            pending_.active = false;
            currentValveMask_ = 0;
            notes_.clear();
            envelope_.reset();
            break;

        default:
            // SysEx and MPE are accepted by the router and simply ignored
            // here until the corresponding feature exists.
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

    const float atWeight = voicing_.aftertouchToBrightness;
    float blow = vel * voicing_.additive.velocityBrightness;
    blow += breath * voicing_.additive.breathBrightness;
    blow += (expr - 0.5f) * voicing_.additive.expressionBrightness;
    blow += pressure * atWeight;
    const float weight =
        voicing_.additive.velocityBrightness + voicing_.additive.breathBrightness + atWeight;
    return clampValue(weight > 0.01f ? blow / weight : vel, 0.0f, 1.0f);
}

void AudioEngine::updateControlRate() {
    // ---- pitch ------------------------------------------------------------
    if (pending_.active) {
        // The pistons are still moving. Whatever the articulation settings say,
        // the pitch stays where it was: gliding - or jumping - to the new note
        // now would play it through the bore of the old fingering, which is the
        // exact defect the synchronisation exists to prevent. Freezing here and
        // not only at the Note On matters because this runs every 32 samples
        // and would otherwise undo the freeze on the very next control tick.
        currentPitch_ = pending_.heldPitch;
    } else if (portamentoCoef_ > 0.0f) {
        currentPitch_ = targetPitch_ + (currentPitch_ - targetPitch_) * portamentoCoef_;
    } else {
        currentPitch_ = targetPitch_;
    }

    const float bendSemitones = (static_cast<float>(pitchBendRaw_) / 8192.0f) *
                                static_cast<float>(pitchBendRange_);

    // ---- vibrato ----------------------------------------------------------
    float vibratoDepth = 0.0f;
    switch (voicing_.vibrato.source) {
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
                          (voicing_.vibrato.depthCents / 100.0f);

    const float pitch = currentPitch_ + bendSemitones + vibrato;
    const float frequency = noteToFrequency(pitch);
    // Reported as the "current frequency": what is really being produced,
    // pitch bend and vibrato included, not just the note that was pressed.
    livePitch_ = pitch;

    // ---- timbre -----------------------------------------------------------
    // The driver, the chamber and the cone are not flat. Correcting that with
    // the master EQ wrecks the timbre, so the correction is a function of the
    // note instead: a little level and a little brightness, interpolated.
    registerCompensation(pitch, registerGain_, registerBrightness_);
    const float blow = clampValue(blowAmount() + registerBrightness_, 0.0f, 1.0f);
    const float pitchNorm = clampValue((pitch - 40.0f) / 50.0f, 0.0f, 1.0f);

    switch (voicing_.engine) {
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
        case SynthEngineType::BRASS_EXCITER:
            exciter_.setFrequency(frequency);
            exciter_.setBlow(blow);
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
    const float breathRaw = ccBreath_ > 0 ? static_cast<float>(ccBreath_) / 127.0f : 1.0f;
    const float pressure = static_cast<float>(channelPressure_) / 127.0f;

    // Each contribution is weighted, so a builder can decide how much of the
    // level a breath controller or an expression pedal really owns. The floor
    // is what a velocity of 1 produces: at 0 the note is a note-off.
    const float floor = clampValue(voicing_.velocityFloor, 0.0f, 1.0f);
    const float velAmp = floor + (1.0f - floor) * vel;
    const float breathAmp = 1.0f - voicing_.breathToVolume * (1.0f - breathRaw);
    const float exprAmp = 1.0f - voicing_.expressionToVolume * (1.0f - expr);
    const float atAmp = 1.0f + voicing_.aftertouchToVolume * pressure;

    float amplitude = velAmp * vol * clampValue(exprAmp, 0.0f, 1.0f)
                    * clampValue(breathAmp, 0.0f, 1.0f) * atAmp * registerGain_;
    smoothedAmplitude_ += (amplitude - smoothedAmplitude_) * 0.25f;
}

void AudioEngine::startArticulation(bool fromSilence) {
    currentPitch_ = (fromSilence || portamentoCoef_ == 0.0f) ? targetPitch_ : currentPitch_;
    envelope_.noteOn(fromSilence || instrument_.retrigger);
    // The exciter models the air column being blown: its pressure has to rise
    // again at every attack, or `transientMs` describes the first note of the
    // session and nothing after it.
    exciter_.noteOn();
    if (fromSilence || instrument_.retrigger) {
        additive_.resetPhase();
        wavetable_.resetPhase();
        exciter_.resetPhase();
    }
    noteAgeSamples_ = 0;
}

uint32_t AudioEngine::valveDelaySamples(uint8_t soundingNote) {
    lastAttackDelayMs_ = 0;
    const uint8_t available = valves_.count >= kMaxValves
                                  ? 0xFFu
                                  : static_cast<uint8_t>((1u << valves_.count) - 1u);
    uint8_t target = transposer_.fingeringForMidiNote(soundingNote);
    target = (target == kNoFingering) ? 0u : static_cast<uint8_t>(target & available);

    const uint16_t ms = noteAttackDelayMs(valves_, currentValveMask_, target);
    currentValveMask_ = target;
    if (ms == 0) return 0;
    lastAttackDelayMs_ = ms;
    return (static_cast<uint32_t>(ms) * sampleRate_) / 1000u;
}

// Counted at block boundaries: 128 frames is 2.7 ms at 48 kHz, well under the
// tens of milliseconds a piston needs, and it keeps the per-sample loop clean.
void AudioEngine::schedulePendingArticulation(size_t frames) {
    if (!pending_.active) return;
    const uint32_t f = static_cast<uint32_t>(frames);
    if (pending_.samples > f) {
        pending_.samples -= f;
        return;
    }
    pending_.samples = 0;
    pending_.active = false;
    if (pending_.articulate) {
        startArticulation(pending_.fromSilence);
    } else if (portamentoCoef_ == 0.0f) {
        // Slurred: no new attack, but the pitch was frozen until now and is
        // released at the moment the pistons arrive.
        currentPitch_ = targetPitch_;
    }
}

// Sustain pedal released: if the key had already come up, the note ends now.
void AudioEngine::releaseSustainedNotes() {
    if (!sustainedNote_) return;
    sustainedNote_ = false;
    if (notes_.hasNote()) return;   // a key is still down, nothing to release
    pending_.active = false;
    lastAttackDelayMs_ = 0;
    currentValveMask_ = 0;
    envelope_.noteOff();
}

// Linear interpolation between the breakpoints. Below the first and above the
// last the end values are held: extrapolating a correction curve is how a
// register fix turns into a broken top octave.
void AudioEngine::registerCompensation(float note, float& gainLinear, float& brightness) const {
    const RegisterPoint* pts = voicing_.registerCurve;
    if (note <= static_cast<float>(pts[0].note)) {
        gainLinear = std::pow(10.0f, pts[0].gainDb / 20.0f);
        brightness = pts[0].brightness;
        return;
    }
    for (uint8_t i = 1; i < kRegisterPoints; ++i) {
        if (note > static_cast<float>(pts[i].note)) continue;
        const float lo = static_cast<float>(pts[i - 1].note);
        const float hi = static_cast<float>(pts[i].note);
        const float t = hi > lo ? (note - lo) / (hi - lo) : 0.0f;
        const float db = pts[i - 1].gainDb + (pts[i].gainDb - pts[i - 1].gainDb) * t;
        brightness = pts[i - 1].brightness + (pts[i].brightness - pts[i - 1].brightness) * t;
        gainLinear = std::pow(10.0f, db / 20.0f);
        return;
    }
    gainLinear = std::pow(10.0f, pts[kRegisterPoints - 1].gainDb / 20.0f);
    brightness = pts[kRegisterPoints - 1].brightness;
}

void AudioEngine::renderBlock(float* out, size_t frames) {
    // Drain the MIDI queue at block boundaries: bounded work, no locking.
    MidiMessage msg;
    uint8_t guard = 0;
    // A new voicing is picked up before the messages, so a preview and the
    // notes that follow it in the same block agree about the sound.
    takePendingVoicing();
    while (guard++ < 32 && queue_.pop(msg)) handleMessage(msg);
    schedulePendingArticulation(frames);

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
            switch (voicing_.engine) {
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
                    const float m = clampValue(voicing_.hybridMix, 0.0f, 1.0f);
                    s = a * m + w * (1.0f - m);
                    break;
                }
                case SynthEngineType::BRASS_EXCITER:
                    // The exciter shapes the air noise itself, so it is handed
                    // the noise sample instead of having it added afterwards.
                    s = exciter_.process(noise_.next());
                    break;
                case SynthEngineType::ADDITIVE:
                default:
                    s = additive_.process();
                    break;
            }
            s *= env;
            // Breath floor plus the short chiff at the very beginning of a
            // note: two low level noise components, they cost one xorshift.
            const float n = noise_.next();
            s += n * (voicing_.envelope.breathNoise * env +
                      voicing_.envelope.attackNoise * envelope_.attackTransient());
            s *= smoothedAmplitude_;
        }

        s = highPass_.process(s);
        for (uint8_t b = 0; b < kMaxEqBands; ++b) s = userEq_[b].process(s);
        for (uint8_t b = 0; b < kMaxEqBands; ++b) s = speakerEq_[b].process(s);
        s *= acousticGainLinear_;
        s = dcBlocker_.process(s);
        dcBlocker_.observe(s);
        // Voicing trim, then master volume, then the limiter. The trim is
        // deliberately upstream of the protection stage: it is a tone control,
        // never a way past the ceiling.
        s *= outputTrimLinear_ * masterVolume_;
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
    // Cleared here as well as by the queued message: if the queue were full
    // the message would be dropped, and a note whose attack is still pending
    // would speak after the panic. A bool store, and it fails towards silence.
    pending_.active = false;
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
