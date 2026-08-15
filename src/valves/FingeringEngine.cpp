#include "valves/FingeringEngine.h"

namespace ot {

namespace {

constexpr uint8_t V1 = 0x01;
constexpr uint8_t V2 = 0x02;
constexpr uint8_t V3 = 0x04;

// Standard chromatic chart, indexed by the number of semitones above written
// F#3 (MIDI 54).  Runs up to written C6 (MIDI 84), which covers the whole
// practical range of the instrument.
constexpr uint8_t kChartBase = 54;  // written F#3
const uint8_t kChart[] = {
    /* F#3 */ V1 | V2 | V3,
    /* G3  */ 0,
    /* G#3 */ V2 | V3,
    /* A3  */ V1 | V2,
    /* Bb3 */ V1,
    /* B3  */ V2,
    /* C4  */ 0,
    /* C#4 */ V1 | V2 | V3,
    /* D4  */ V1 | V3,
    /* Eb4 */ V2 | V3,
    /* E4  */ V1 | V2,
    /* F4  */ V1,
    /* F#4 */ V2,
    /* G4  */ 0,
    /* G#4 */ V2 | V3,
    /* A4  */ V1 | V2,
    /* Bb4 */ V1,
    /* B4  */ V2,
    /* C5  */ 0,
    /* C#5 */ V1 | V2,
    /* D5  */ V1,
    /* Eb5 */ V2,
    /* E5  */ 0,
    /* F5  */ V1,
    /* F#5 */ V2,
    /* G5  */ 0,
    /* G#5 */ V2 | V3,
    /* A5  */ V1 | V2,
    /* Bb5 */ V1,
    /* B5  */ V2,
    /* C6  */ 0,
};
constexpr int kChartSize = static_cast<int>(sizeof(kChart));

}  // namespace

bool FingeringEngine::inStandardRange(int writtenNote) {
    return writtenNote >= kChartBase && writtenNote < kChartBase + kChartSize;
}

uint8_t FingeringEngine::defaultPrimary(int writtenNote) {
    if (writtenNote < 0 || writtenNote > 127) return kNoFingering;
    if (inStandardRange(writtenNote)) return kChart[writtenNote - kChartBase];

    // Outside the practical range the fingering pattern still repeats every
    // octave, so the table stays useful for pedal tones and for the extreme
    // high register instead of leaving a hole.
    int n = writtenNote;
    while (n < kChartBase) n += 12;
    while (n >= kChartBase + kChartSize) n -= 12;
    if (!inStandardRange(n)) return kNoFingering;
    return kChart[n - kChartBase];
}

uint8_t FingeringEngine::defaultAlternate(int writtenNote) {
    const uint8_t p = defaultPrimary(writtenNote);
    if (p == kNoFingering) return kNoFingering;
    // Valve 3 lengthens the tube by three semitones, exactly like valves 1+2
    // together, so those two grips are genuinely interchangeable.  Every other
    // combination has no equal-length twin and therefore no alternate.
    if (p == (V1 | V2)) return V3;
    if (p == V3) return V1 | V2;
    return kNoFingering;
}

void FingeringEngine::configure(const InstrumentConfig& cfg) {
    cfg_ = cfg;
    if (!initialised_) resetToDefault();
}

void FingeringEngine::resetToDefault() {
    for (int n = 0; n < 128; ++n) {
        primary_[n] = defaultPrimary(n);
        alternate_[n] = defaultAlternate(n);
    }
    initialised_ = true;
}

int8_t FingeringEngine::transposeSemitones() const {
    switch (cfg_.type) {
        case InstrumentType::BB_TRUMPET:
            return 2;   // written C sounds Bb
        case InstrumentType::C_TRUMPET:
            return 0;
        case InstrumentType::EB_TRUMPET:
            return -3;  // written C sounds Eb above
        case InstrumentType::CUSTOM:
        default:
            return cfg_.customTransposeSemitones;
    }
}

int FingeringEngine::soundingNote(uint8_t midiNote) const {
    if (cfg_.pitchMode == PitchInterpretation::CONCERT) return midiNote;
    return static_cast<int>(midiNote) - transposeSemitones();
}

int FingeringEngine::writtenNote(uint8_t midiNote) const {
    if (cfg_.pitchMode == PitchInterpretation::WRITTEN) return midiNote;
    return static_cast<int>(midiNote) + transposeSemitones();
}

uint8_t FingeringEngine::primary(uint8_t writtenNoteIndex) const {
    return initialised_ ? primary_[writtenNoteIndex] : defaultPrimary(writtenNoteIndex);
}

uint8_t FingeringEngine::alternate(uint8_t writtenNoteIndex) const {
    return initialised_ ? alternate_[writtenNoteIndex] : defaultAlternate(writtenNoteIndex);
}

bool FingeringEngine::setFingering(uint8_t writtenNoteIndex, uint8_t primaryMask,
                                   uint8_t alternateMask) {
    if (!initialised_) resetToDefault();
    if (primaryMask != kNoFingering && (primaryMask & 0xF0) != 0) return false;
    if (alternateMask != kNoFingering && (alternateMask & 0xF0) != 0) return false;
    primary_[writtenNoteIndex] = primaryMask;
    alternate_[writtenNoteIndex] = alternateMask;
    return true;
}

uint8_t FingeringEngine::fingeringForMidiNote(uint8_t midiNote, bool useAlternate) const {
    const int written = writtenNote(midiNote);
    if (written < 0 || written > 127) return kNoFingering;
    const uint8_t alt = alternate(static_cast<uint8_t>(written));
    if (useAlternate && alt != kNoFingering) return alt;
    return primary(static_cast<uint8_t>(written));
}

}  // namespace ot
