#include "midi/NoteStack.h"

namespace ot {

bool NoteStack::contains(uint8_t note) const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (notes_[i].note == note) return true;
    }
    return false;
}

bool NoteStack::noteOn(uint8_t note, uint8_t velocity) {
    const uint8_t previous = count_ ? active_.note : 0xFF;

    // Re-triggering a held note refreshes its velocity and its order.
    for (uint8_t i = 0; i < count_; ++i) {
        if (notes_[i].note == note) {
            notes_[i].velocity = velocity;
            notes_[i].order = ++counter_;
            recomputeActive();
            return count_ && (active_.note != previous);
        }
    }

    if (count_ >= kNoteStackDepth) {
        // Drop the oldest entry rather than the new one: on a monophonic
        // instrument the player always expects the newest key to matter.
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < count_; ++i) {
            if (notes_[i].order < notes_[oldest].order) oldest = i;
        }
        for (uint8_t i = oldest; i + 1 < count_; ++i) notes_[i] = notes_[i + 1];
        --count_;
    }

    notes_[count_].note = note;
    notes_[count_].velocity = velocity;
    notes_[count_].order = ++counter_;
    ++count_;
    recomputeActive();
    return active_.note != previous;
}

bool NoteStack::noteOff(uint8_t note) {
    const bool had = count_ > 0;
    const uint8_t previous = had ? active_.note : 0xFF;

    for (uint8_t i = 0; i < count_; ++i) {
        if (notes_[i].note != note) continue;
        for (uint8_t j = i; j + 1 < count_; ++j) notes_[j] = notes_[j + 1];
        --count_;
        recomputeActive();
        if (count_ == 0) return had;
        return active_.note != previous;
    }
    return false;
}

void NoteStack::clear() {
    count_ = 0;
    active_ = HeldNote();
}

void NoteStack::recomputeActive() {
    if (count_ == 0) {
        active_ = HeldNote();
        return;
    }
    uint8_t best = 0;
    for (uint8_t i = 1; i < count_; ++i) {
        switch (priority_) {
            case NotePriority::HIGHEST:
                if (notes_[i].note > notes_[best].note) best = i;
                break;
            case NotePriority::LOWEST:
                if (notes_[i].note < notes_[best].note) best = i;
                break;
            case NotePriority::LAST:
            default:
                if (notes_[i].order > notes_[best].order) best = i;
                break;
        }
    }
    active_ = notes_[best];
}

}  // namespace ot
