#include "config/ConfigMigration.h"

#include "audio/Profiles.h"
#include "midi/MidiRouter.h"

namespace ot {

namespace {

// v1 -> v2
//   * v1 had no MIDI routing matrix at all: inputs were hard wired to both
//     engines.  Rebuild that behaviour explicitly so the instrument keeps
//     playing exactly as before the update.
//   * v1 stored no acoustic coupling; the reference build has always been a
//     sealed chamber, so that is the honest default for an existing user.
void upgrade1to2(InstrumentConfiguration& cfg) {
    if (cfg.midi.routeCount == 0) MidiRouter::makeDefaultRoutes(cfg.midi);
    if (cfg.acoustic.rearChamberVolumeMl <= 0.0f) {
        cfg.acoustic = AcousticConfig();
    }
    cfg.schemaVersion = 2;
}

// v2 -> v3
//   * the acoustic block described a single chamber and a single outlet.  The
//     real assembly is a sealed rear chamber, a front chamber and a two-stage
//     compression cone, so the geometry is now described properly.  The reader
//     already maps the old fields onto the closest new ones; what is left here
//     is filling the parts a v2 file never had with the reference geometry.
//   * the speaker catalogue gained the manufacturer *maximum* rating and the
//     Thiele-Small parameters.  A v2 file has neither: re-apply the catalogue
//     row so a known driver picks up the corrected figures, and in particular
//     so a Monacor SPX-30M stops claiming a 30 W continuous rating.
//   * valve -> audio synchronisation did not exist.  It is enabled by default,
//     which is the behaviour a real instrument wants.
void upgrade2to3(InstrumentConfiguration& cfg) {
    if (cfg.acoustic.stage1.inletDiameterMm <= 0.0f) cfg.acoustic.stage1 = HornStageConfig();
    if (cfg.acoustic.stage2.outletDiameterMm <= 0.0f) {
        cfg.acoustic.stage2 = HornStageConfig{28.0f, 12.0f, 40.0f};
    }
    if (cfg.acoustic.frontChamberVolumeMl <= 0.0f) cfg.acoustic.frontChamberVolumeMl = 35.0f;
    if (cfg.acoustic.leadpipeDiameterMm <= 0.0f) cfg.acoustic.leadpipeDiameterMm = 11.0f;
    if (cfg.acoustic.rearChamberVolumeMl <= 0.0f) cfg.acoustic.rearChamberVolumeMl = 120.0f;

    if (cfg.speaker.profile != SpeakerProfileId::CUSTOM) {
        // Deliberately overwrites: the old numbers were wrong, and a stored
        // file must not keep a rating that is above what the driver is rated
        // for.  A CUSTOM speaker is the user's own and is left alone.
        applySpeakerProfileDefaults(cfg.speaker.profile, cfg.speaker);
    } else {
        // A v2 file has no maximum rating at all - it is a v3 field.  The only
        // honest value for a driver the builder characterised themselves is
        // their own continuous rating, not whatever the struct happened to
        // default to.
        cfg.speaker.powerMaxW = cfg.speaker.powerRmsW;
    }

    cfg.valves.sync = ValveSyncConfig();
    cfg.schemaVersion = 3;
}

// v3 -> v4
//   * the sound moved out of AudioConfig into its own VoicingConfig, so it can
//     be replaced live without going anywhere near the pins, the impedance or
//     the protection stage.  The reader already maps a v3 `audio` block onto
//     the new voicing, so nothing is lost.
//   * the user EQ went from three bands to six.  A v3 file fills the first
//     three; the rest start disabled at sensible frequencies.
//   * the spectral tilts, the hybrid mix, the velocity floor and the
//     aftertouch weights were literals in the DSP.  A migrated instrument gets
//     exactly the values that used to be compiled in, so it sounds identical.
void upgrade3to4(InstrumentConfiguration& cfg) {
    const VoicingConfig defaults;
    for (uint8_t i = 3; i < kMaxEqBands; ++i) {
        if (cfg.voicing.eq[i].frequency <= 0.0f) cfg.voicing.eq[i] = defaults.eq[i];
    }
    if (cfg.voicing.name[0] == '\0') copyString(cfg.voicing.name, kNameLen, "Natural");
    if (cfg.voicing.darkTilt <= 0.0f) cfg.voicing.darkTilt = 2.6f;
    if (cfg.voicing.brightTilt <= 0.0f) cfg.voicing.brightTilt = 0.6f;
    if (cfg.voicing.hybridMix <= 0.0f) cfg.voicing.hybridMix = 0.6f;
    if (cfg.voicing.velocityFloor <= 0.0f) cfg.voicing.velocityFloor = 0.25f;
    if (cfg.voicing.aftertouchToBrightness <= 0.0f) cfg.voicing.aftertouchToBrightness = 0.25f;
    if (cfg.voicing.pitchBendRangeSemitones == 0) cfg.voicing.pitchBendRangeSemitones = 2;
    cfg.schemaVersion = 4;
}

}  // namespace

MigrationResult migrateConfig(InstrumentConfiguration& cfg) {
    MigrationResult result;
    result.fromVersion = cfg.schemaVersion;
    result.toVersion = cfg.schemaVersion;

    if (cfg.schemaVersion > kConfigSchemaVersion) {
        // A configuration written by a newer firmware.  Refuse to guess: the
        // caller falls back to the defaults and tells the user.
        result.unsupported = true;
        return result;
    }

    while (cfg.schemaVersion < kConfigSchemaVersion) {
        const uint16_t before = cfg.schemaVersion;
        switch (cfg.schemaVersion) {
            case 0:
            case 1:
                upgrade1to2(cfg);
                break;
            case 2:
                upgrade2to3(cfg);
                break;
            case 3:
                upgrade3to4(cfg);
                break;
            default:
                cfg.schemaVersion = kConfigSchemaVersion;
                break;
        }
        result.migrated = true;
        if (cfg.schemaVersion <= before) {
            // Defensive: never loop forever on a malformed version number.
            cfg.schemaVersion = kConfigSchemaVersion;
            break;
        }
    }

    result.toVersion = cfg.schemaVersion;
    return result;
}

}  // namespace ot
