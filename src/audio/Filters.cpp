#include "audio/Filters.h"

#include <cmath>

namespace ot {

namespace {
constexpr float kPi = 3.14159265358979f;
}

void Biquad::reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

void Biquad::normalise(float b0, float b1, float b2, float a0, float a1, float a2) {
    if (a0 == 0.0f) a0 = 1.0f;
    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
    bypass_ = false;
}

void Biquad::setHighPass(float f, float q, uint32_t sampleRate) {
    if (f <= 0.0f || sampleRate == 0) {
        bypass_ = true;
        return;
    }
    f = clampValue(f, 1.0f, static_cast<float>(sampleRate) * 0.45f);
    if (q < 0.05f) q = 0.05f;
    const float w = 2.0f * kPi * f / static_cast<float>(sampleRate);
    const float cw = std::cos(w), sw = std::sin(w);
    const float alpha = sw / (2.0f * q);
    normalise((1.0f + cw) * 0.5f, -(1.0f + cw), (1.0f + cw) * 0.5f, 1.0f + alpha, -2.0f * cw,
              1.0f - alpha);
}

void Biquad::setLowPass(float f, float q, uint32_t sampleRate) {
    if (f <= 0.0f || sampleRate == 0) {
        bypass_ = true;
        return;
    }
    f = clampValue(f, 1.0f, static_cast<float>(sampleRate) * 0.45f);
    if (q < 0.05f) q = 0.05f;
    const float w = 2.0f * kPi * f / static_cast<float>(sampleRate);
    const float cw = std::cos(w), sw = std::sin(w);
    const float alpha = sw / (2.0f * q);
    normalise((1.0f - cw) * 0.5f, 1.0f - cw, (1.0f - cw) * 0.5f, 1.0f + alpha, -2.0f * cw,
              1.0f - alpha);
}

void Biquad::setPeaking(float f, float gainDb, float q, uint32_t sampleRate) {
    if (f <= 0.0f || sampleRate == 0 || std::fabs(gainDb) < 0.01f) {
        bypass_ = true;
        return;
    }
    f = clampValue(f, 1.0f, static_cast<float>(sampleRate) * 0.45f);
    if (q < 0.05f) q = 0.05f;
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w = 2.0f * kPi * f / static_cast<float>(sampleRate);
    const float cw = std::cos(w), sw = std::sin(w);
    const float alpha = sw / (2.0f * q);
    normalise(1.0f + alpha * A, -2.0f * cw, 1.0f - alpha * A, 1.0f + alpha / A, -2.0f * cw,
              1.0f - alpha / A);
}

void Biquad::setHighShelf(float f, float gainDb, uint32_t sampleRate) {
    if (f <= 0.0f || sampleRate == 0 || std::fabs(gainDb) < 0.01f) {
        bypass_ = true;
        return;
    }
    f = clampValue(f, 1.0f, static_cast<float>(sampleRate) * 0.45f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w = 2.0f * kPi * f / static_cast<float>(sampleRate);
    const float cw = std::cos(w), sw = std::sin(w);
    const float alpha = sw * 0.5f * std::sqrt((A + 1.0f / A) * (1.0f / 0.9f - 1.0f) + 2.0f);
    const float twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;
    normalise(A * ((A + 1.0f) + (A - 1.0f) * cw + twoSqrtAalpha),
              -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw),
              A * ((A + 1.0f) + (A - 1.0f) * cw - twoSqrtAalpha),
              (A + 1.0f) - (A - 1.0f) * cw + twoSqrtAalpha,
              2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
              (A + 1.0f) - (A - 1.0f) * cw - twoSqrtAalpha);
}

void DcBlocker::setSampleRate(uint32_t sampleRate) {
    if (sampleRate == 0) sampleRate = 48000;
    // ~5 Hz corner.
    r_ = 1.0f - (2.0f * kPi * 5.0f / static_cast<float>(sampleRate));
    if (r_ > 0.99999f) r_ = 0.99999f;
    reset();
}

}  // namespace ot
