// ============================================================================
//  FingeringEngine.h - MIDI note -> valve bitmask.
//
//  Bit 0 = valve 1, bit 1 = valve 2, bit 2 = valve 3, bit 3 = valve 4.
//  The table is fully editable from the web UI (edit / save / reset / import /
//  export); the defaults below are the standard chromatic trumpet chart.
//
//  Transposition is never hard coded: the engine converts the incoming MIDI
//  note to *written* pitch using the instrument settings and looks the written
//  pitch up in the table.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

static constexpr uint8_t kNoFingering = 0xFF;

class FingeringEngine {
public:
    void configure(const InstrumentConfig& cfg);
    const InstrumentConfig& config() const { return cfg_; }

    // written - concert, in semitones (Bb trumpet = +2).
    int8_t transposeSemitones() const;

    // Note actually produced by the sound engine (concert / sounding pitch).
    int soundingNote(uint8_t midiNote) const;
    // Note used to look up a fingering.
    int writtenNote(uint8_t midiNote) const;

    // Returns kNoFingering when the written pitch falls outside the table.
    uint8_t fingeringForMidiNote(uint8_t midiNote, bool useAlternate = false) const;

    // Direct access to the (written pitch indexed) table.
    uint8_t primary(uint8_t writtenNote) const;
    uint8_t alternate(uint8_t writtenNote) const;
    bool setFingering(uint8_t writtenNote, uint8_t primaryMask, uint8_t alternateMask);
    void resetToDefault();

    static uint8_t defaultPrimary(int writtenNote);
    static uint8_t defaultAlternate(int writtenNote);
    // True when the written pitch is inside the practical trumpet range.
    static bool inStandardRange(int writtenNote);

private:
    InstrumentConfig cfg_;
    uint8_t primary_[128];
    uint8_t alternate_[128];
    bool initialised_ = false;
};

}  // namespace ot
