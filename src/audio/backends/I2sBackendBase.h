// ============================================================================
//  I2sBackendBase.h - shared I2S standard-mode transmitter.
//
//  PCM5102A, MAX98357A, TAS5760M, ES8388 and WM8960 all speak plain I2S; they
//  only differ by their control interface and by the bit depth they accept.
//  Everything common lives here, each concrete backend only adds its own
//  power-up / register sequence.
//
//  Uses the ESP-IDF 5 i2s_std driver with DMA.  writeSamples() is the only
//  blocking call in the audio task and it blocks on the DMA ring, which is
//  exactly the timing reference we want.
// ============================================================================
#pragma once

#include "audio/IAudioBackend.h"

#if !defined(OT_HOST_BUILD)
#include <driver/i2s_std.h>
#endif

namespace ot {

class I2sBackendBase : public IAudioBackend {
public:
    ~I2sBackendBase() override;

    void configure(const AudioConfig& cfg) override;
    bool begin() override;
    void end() override;

    void writeSamples(const int16_t* buffer, size_t count) override;
    void writeSamples32(const int32_t* buffer, size_t count) override;

    void mute(bool state) override;
    bool isMuted() const override { return muted_; }

    uint32_t sampleRate() const override { return cfg_.sampleRate; }
    uint8_t bitDepth() const override { return bitDepth_; }
    bool isRunning() const override { return running_; }
    uint32_t underruns() const override { return underruns_; }
    void resetUnderruns() override { underruns_ = 0; }
    const char* lastError() const override { return error_[0] ? error_ : nullptr; }

protected:
    // Hooks for the concrete backends.
    virtual bool preparePeripheral() { return true; }   // before I2S starts
    virtual bool startCodec() { return true; }          // after I2S starts
    virtual void stopCodec() {}
    virtual void applyMute(bool state) {}
    // 16, 24 or 32; the base class clamps the requested depth to this.
    virtual uint8_t maximumBitDepth() const { return 32; }

    bool installI2s();
    void setError(const char* text);

    AudioConfig cfg_;
    uint8_t bitDepth_ = 16;
    bool muted_ = true;
    bool running_ = false;
    uint32_t underruns_ = 0;
    char error_[64] = "";

#if !defined(OT_HOST_BUILD)
    i2s_chan_handle_t tx_ = nullptr;
    i2s_chan_handle_t rx_ = nullptr;
#endif
};

}  // namespace ot
