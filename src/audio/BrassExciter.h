// ============================================================================
//  BrassExciter.h - EXPERIMENTAL non-linear exciter.
//
//  This is NOT a physical model of a trumpet, and it deliberately is not one:
//  the real trumpet is the resonator. Everything downstream of the cone is
//  brass and air, so there is nothing left to simulate there. What the
//  additive engine cannot do is behave like a lip reed - produce harmonics
//  that appear because the excitation is being driven harder, rather than
//  because a gain table said so.
//
//      oscillator  ->  x drive(pressure)  ->  asymmetric shaper  ->  out
//                                   ^
//                          air noise mixed in before the shaper,
//                          so it is shaped too and not just added
//
//  The asymmetry is what produces even harmonics; a symmetric shaper only ever
//  gives odd ones and sounds like a clarinet. `pressure` rises over
//  `transientMs` at the start of a note, which is what gives the attack its
//  character rather than an amplitude envelope alone.
//
//  Status: EXPERIMENTAL. It has never been heard through a real cone. It is
//  offered as a second engine to characterise against ADDITIVE, which stays
//  the reference, and the UI says so.
// ============================================================================
#pragma once

#include "audio/Oscillator.h"
#include "config/ConfigTypes.h"

namespace ot {

class BrassExciter {
public:
    void configure(const ExciterConfig& cfg, uint32_t sampleRate);

    void setFrequency(float hz);
    // 0..1, how hard the instrument is being blown.
    void setBlow(float blow) { blow_ = clampValue(blow, 0.0f, 1.0f); }
    void noteOn();
    void resetPhase();

    // `noise` is a -1..1 sample from the shared noise source: the exciter does
    // not own one, so the whole engine keeps a single generator.
    float process(float noise);

private:
    ExciterConfig cfg_;
    Oscillator osc_;
    uint32_t sampleRate_ = 48000;
    float blow_ = 0.0f;
    float pressure_ = 0.0f;      // smoothed, rises over transientMs
    float pressureCoef_ = 0.0f;
    float normalisation_ = 1.0f;
};

}  // namespace ot
