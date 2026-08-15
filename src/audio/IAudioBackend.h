// ============================================================================
//  IAudioBackend.h - the only thing the audio engine knows about the output.
//
//  Replacing a PCM5102A by a MAX98357A, an ES8388 or a TAS5760M is a matter of
//  instantiating a different implementation of this interface.  The engine
//  never learns which one it is talking to.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

// How much the project has actually validated a backend.  It is surfaced in
// the web UI as a badge so nothing is ever presented as more finished than it
// is (see the development rules in docs/ARCHITECTURE.md).
enum class BackendMaturity : uint8_t {
    STABLE = 0,       // validated on the reference hardware
    EXPERIMENTAL,     // implemented and compiled, not validated on silicon yet
    PROTOTYPE         // usable for a bring-up test only, poor quality by design
};

const char* toString(BackendMaturity m);

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    // The configuration is handed over before begin() so a backend can refuse
    // an impossible request (e.g. internal DAC at 48 kHz / 24 bit).
    virtual void configure(const AudioConfig& cfg) = 0;
    virtual bool begin() = 0;
    virtual void end() {}

    // Blocking write into the I2S DMA ring.  Called only from the audio task.
    virtual void writeSamples(const int16_t* buffer, size_t count) = 0;
    // Q31 variant used when the backend runs at more than 16 bits.  The
    // default implementation narrows to 16 bits so a backend only has to
    // implement the width it really supports.
    virtual void writeSamples32(const int32_t* buffer, size_t count);

    virtual void mute(bool state) = 0;
    virtual bool isMuted() const = 0;

    virtual const char* name() const = 0;
    virtual uint32_t sampleRate() const = 0;
    virtual uint8_t bitDepth() const = 0;
    virtual bool isRunning() const = 0;
    // Counted by the backend whenever the DMA ran dry.
    virtual uint32_t underruns() const { return 0; }
    virtual void resetUnderruns() {}
    virtual bool supportsCapture() const { return false; }
    virtual const char* lastError() const { return nullptr; }
    virtual BackendMaturity maturity() const { return BackendMaturity::EXPERIMENTAL; }
};

}  // namespace ot
