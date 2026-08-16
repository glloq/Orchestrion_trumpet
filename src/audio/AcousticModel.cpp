#include "audio/AcousticModel.h"

#include <cmath>

namespace ot {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kSpeedOfSound = 343.0f;   // m/s, dry air at 20 C

float circleAreaMm2(float diameterMm) {
    if (diameterMm <= 0.0f) return 0.0f;
    const float r = diameterMm * 0.5f;
    return kPi * r * r;
}

// Half angle of a cone that goes from `inlet` to `outlet` over `length`.
// A steep cone reflects rather than transforms, so this is worth reporting.
float halfAngleDeg(const HornStageConfig& stage) {
    if (stage.lengthMm <= 0.0f) return 90.0f;
    const float dr = (stage.inletDiameterMm - stage.outletDiameterMm) * 0.5f;
    return std::atan(dr / stage.lengthMm) * 180.0f / kPi;
}

}  // namespace

AcousticModel computeAcousticModel(const AcousticConfig& cfg, const SpeakerConfig& speaker) {
    AcousticModel m;

    if (cfg.coupling == AcousticCouplingType::OPEN_AIR) {
        // Nothing loads the driver: there is no coupling to model and no
        // reason to high pass beyond what the driver itself asks for.
        m.highPassHz = 0.0f;
        m.highPassSource = AcousticSource::NOT_APPLICABLE;
        m.gainDb = 0.0f;
        return m;
    }

    m.gainDb = cfg.eqGainDb;

    // ---- pure geometry ----------------------------------------------------
    m.coneAreaMm2 = circleAreaMm2(cfg.stage1.inletDiameterMm);
    m.throatAreaMm2 = circleAreaMm2(cfg.leadpipeDiameterMm);
    if (m.throatAreaMm2 > 0.0f) m.compressionRatio = m.coneAreaMm2 / m.throatAreaMm2;
    m.stage1HalfAngleDeg = halfAngleDeg(cfg.stage1);
    m.stage2HalfAngleDeg = halfAngleDeg(cfg.stage2);
    m.totalPathLengthMm = cfg.stage1.lengthMm + cfg.intermediateLengthMm + cfg.stage2.lengthMm;

    // ---- front chamber upper corner ---------------------------------------
    // First-order Helmholtz: the trapped volume in front of the cone is the
    // compliance, the air in the cone assembly is the mass.
    //
    //     f = (c / 2*pi) * sqrt(A / (V * Leff))
    //
    // A is the narrowest section, Leff the physical path plus one flanged end
    // correction (0.85 * radius).  This is an estimate of where the front
    // cavity resonates, not a filter response and not a measurement.
    const float volumeM3 = cfg.frontChamberVolumeMl * 1.0e-6f;
    const float areaM2 = m.throatAreaMm2 * 1.0e-6f;
    const float endCorrectionMm = 0.85f * cfg.leadpipeDiameterMm * 0.5f;
    const float lengthM = (m.totalPathLengthMm + endCorrectionMm) * 1.0e-3f;
    if (volumeM3 > 0.0f && areaM2 > 0.0f && lengthM > 0.0f) {
        m.helmholtzResonanceHz =
            (kSpeedOfSound / (2.0f * kPi)) * std::sqrt(areaM2 / (volumeM3 * lengthM));
    }

    // ---- sealed rear chamber ----------------------------------------------
    // f_c = fs * sqrt(1 + Vas / Vb).  Only computed when the driver's Thiele-
    // Small parameters have actually been entered; otherwise the model reports
    // "unknown" rather than a plausible-looking invention.
    const float vbLitres = cfg.rearChamberVolumeMl * 0.001f;
    if (speaker.fsHz > 0.0f && speaker.vasLitres > 0.0f && vbLitres > 0.0f) {
        m.sealedResonanceHz = speaker.fsHz * std::sqrt(1.0f + speaker.vasLitres / vbLitres);
        m.sealedResonanceKnown = true;
    }

    // ---- the high pass the DSP will use -----------------------------------
    if (cfg.measuredHighPassHz > 0.0f) {
        m.highPassHz = cfg.measuredHighPassHz;
        m.highPassSource = AcousticSource::MEASURED;
    } else if (m.sealedResonanceKnown) {
        // Protect below the sealed resonance: under it the cone moves without
        // producing output and only burns excursion.
        m.highPassHz = m.sealedResonanceHz;
        m.highPassSource = AcousticSource::DERIVED;
    } else {
        m.highPassHz = speaker.recommendedHighPassHz;
        m.highPassSource = AcousticSource::SPEAKER_PROFILE;
    }

    return m;
}

}  // namespace ot
