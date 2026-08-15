// ============================================================================
//  AudioBackendFactory.h - the single place where a backend type becomes an
//  object.  Adding a new DAC means adding one case here and one file under
//  audio/backends: nothing else in the firmware changes.
// ============================================================================
#pragma once

#include "audio/IAudioBackend.h"

namespace ot {

struct BackendDescriptor {
    AudioBackendType type;
    const char* key;
    const char* title;
    const char* summary;
    BackendMaturity maturity;
    bool needsI2c;
    bool needsSdPin;
    bool supportsCapture;
    uint8_t maxBitDepth;
};

uint8_t audioBackendCount();
const BackendDescriptor& audioBackendDescriptor(uint8_t index);
const BackendDescriptor* findAudioBackend(AudioBackendType type);

// Instantiates a backend.  Ownership stays with the caller; returns nullptr
// only for a type the current board cannot support.
IAudioBackend* createAudioBackend(AudioBackendType type);
void destroyAudioBackend(IAudioBackend* backend);

}  // namespace ot
