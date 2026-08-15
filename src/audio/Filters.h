// ============================================================================
//  Filters.h - biquads used by the voicing, the speaker compensation and the
//  protection stage.  Direct form I, float, no allocation.
// ============================================================================
#pragma once

#include "core/Platform.h"

namespace ot {

class Biquad {
public:
    void reset();
    void setBypass(bool bypass) { bypass_ = bypass; }
    bool bypassed() const { return bypass_; }

    void setHighPass(float frequencyHz, float q, uint32_t sampleRate);
    void setLowPass(float frequencyHz, float q, uint32_t sampleRate);
    void setPeaking(float frequencyHz, float gainDb, float q, uint32_t sampleRate);
    void setHighShelf(float frequencyHz, float gainDb, uint32_t sampleRate);

    float process(float x) {
        if (bypass_) return x;
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    void normalise(float b0, float b1, float b2, float a0, float a1, float a2);

    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
    bool bypass_ = true;
};

// Removes any DC offset before the amplifier: a DC component in a class-D
// stage is a direct route to a burnt voice coil.
class DcBlocker {
public:
    void setSampleRate(uint32_t sampleRate);
    void reset() { x1_ = y1_ = 0.0f; }

    float process(float x) {
        const float y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }
    // Slow moving average of the output, used by the DC fault detector.
    float dcEstimate() const { return dc_; }
    void observe(float x) { dc_ += (x - dc_) * 0.00002f; }

private:
    float r_ = 0.9995f;
    float x1_ = 0.0f, y1_ = 0.0f, dc_ = 0.0f;
};

}  // namespace ot
