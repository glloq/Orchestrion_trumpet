// ============================================================================
//  IAudioEngine.h - contract between the MIDI world and the sound generation.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    virtual bool begin() = 0;
    // Renders `frames` mono samples in [-1, 1].  Called only from the audio
    // task, never blocks, never allocates.
    virtual void renderBlock(float* out, size_t frames) = 0;
    virtual void allNotesOff() = 0;
    virtual void panic() = 0;
    virtual void setMasterVolume(float volume) = 0;
    virtual float masterVolume() const = 0;
};

}  // namespace ot
