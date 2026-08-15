// ============================================================================
//  NoteStack.h - monophonic note allocation.
//
//  A trumpet plays one note at a time, but a keyboard, a sequencer or a DAW
//  will happily send overlapping Note On messages.  This stack keeps every
//  held note and answers "which one should sound right now" according to the
//  configured priority.  Both the sound engine and the valve engine use it, so
//  they never disagree about the current note.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

static constexpr uint8_t kNoteStackDepth = 12;

struct HeldNote {
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint32_t order = 0;   // monotonic, used by LAST priority
};

class NoteStack {
public:
    void setPriority(NotePriority p) { priority_ = p; }
    NotePriority priority() const { return priority_; }

    // Returns true when the resulting active note changed.
    bool noteOn(uint8_t note, uint8_t velocity);
    bool noteOff(uint8_t note);
    void clear();

    bool hasNote() const { return count_ > 0; }
    uint8_t count() const { return count_; }
    // Valid only when hasNote() is true.
    uint8_t activeNote() const { return active_.note; }
    uint8_t activeVelocity() const { return active_.velocity; }
    bool contains(uint8_t note) const;

private:
    void recomputeActive();

    NotePriority priority_ = NotePriority::LAST;
    HeldNote notes_[kNoteStackDepth];
    uint8_t count_ = 0;
    uint32_t counter_ = 0;
    HeldNote active_;
};

}  // namespace ot
