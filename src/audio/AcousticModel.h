// ============================================================================
//  AcousticModel.h - geometry of the coupling between the driver and the bore.
//
//  The mechanical assembly is:
//
//      sealed rear chamber
//            |
//         [driver]
//            |
//      front chamber            small trapped volume in front of the cone
//            |
//      stage 1 cone             cone diameter -> intermediate diameter
//            |
//      intermediate tube
//            |
//      stage 2 cone             intermediate diameter -> leadpipe diameter
//            |
//      trumpet leadpipe
//
//  Everything this file computes follows from that geometry and from the speed
//  of sound.  Nothing here is a measurement and nothing here pretends to be
//  one: the numbers are STARTING POINTS for the bench, and every one of them
//  can be overridden by a measured value once the instrument exists.  That is
//  the whole contract - `AcousticModel::derived` tells you where the estimate
//  came from, so a measured figure is never silently confused with a guessed
//  one.
//
//  Deliberately NOT modelled: the driver's own sealed-box resonance shift,
//  which needs Vas and fs.  Those are Thiele-Small parameters of the specific
//  driver; when the builder enters them the model uses them, and until then it
//  says so instead of inventing them.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

// Where a number came from.  Reported to the UI so an estimate is never shown
// as if it had been measured.
enum class AcousticSource : uint8_t {
    NOT_APPLICABLE = 0,   // open air: there is no coupling to model
    MEASURED,             // the builder entered a bench figure
    DERIVED,              // computed from the geometry below
    SPEAKER_PROFILE       // fell back to the driver's own recommendation
};

struct AcousticModel {
    // ---- geometry ---------------------------------------------------------
    float coneAreaMm2 = 0.0f;
    float throatAreaMm2 = 0.0f;        // narrowest section, i.e. the leadpipe
    float compressionRatio = 0.0f;     // cone area / throat area
    float stage1HalfAngleDeg = 0.0f;
    float stage2HalfAngleDeg = 0.0f;
    float totalPathLengthMm = 0.0f;

    // ---- derived resonances -----------------------------------------------
    // Helmholtz resonance of the trapped front volume working against the mass
    // of air in the cone assembly, to first order.  It is a RESONANCE, not a
    // filter corner: the response neither starts nor stops there.  What it is
    // good for is spotting a geometry whose front cavity resonates in the
    // middle of the instrument's range - which is audible, and which no amount
    // of EQ fixes.  The real curve has to come from a sweep and a microphone.
    float helmholtzResonanceHz = 0.0f;
    // The high pass the DSP will actually apply, and where it came from.
    float highPassHz = 0.0f;
    AcousticSource highPassSource = AcousticSource::NOT_APPLICABLE;
    // Sealed rear chamber resonance, only when fs and Vas are known.
    float sealedResonanceHz = 0.0f;
    bool sealedResonanceKnown = false;

    float gainDb = 0.0f;
};

// `speaker` supplies fs / Vas when the builder has entered them, and the
// fallback high pass when the geometry cannot produce one.
AcousticModel computeAcousticModel(const AcousticConfig& cfg, const SpeakerConfig& speaker);

}  // namespace ot
