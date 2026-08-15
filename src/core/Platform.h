// ============================================================================
//  Platform.h - thin compatibility shim.
//
//  A large part of the firmware logic (configuration, MIDI parsing/routing,
//  fingering, limiter maths, actuator safety) is deliberately free of any
//  Arduino dependency so that it can be compiled and unit tested on the host
//  (`pio test -e native`).  Those translation units include this header
//  instead of <Arduino.h>.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(OT_HOST_BUILD)

#include <algorithm>
#include <string>

// Minimal stand-ins used by the host build.  The clock is driven by hand so a
// solenoid timeout or a servo ramp can be verified in microseconds of test
// time instead of seconds of real time.
namespace ot {
inline uint32_t& hostMillisRef() {
    static uint32_t value = 0;
    return value;
}
inline uint32_t hostMillis() { return hostMillisRef(); }
inline void hostSetMillis(uint32_t ms) { hostMillisRef() = ms; }
inline void hostAdvanceMillis(uint32_t ms) { hostMillisRef() += ms; }
}  // namespace ot

#define OT_MILLIS() ::ot::hostMillis()
#define OT_MICROS() (::ot::hostMillis() * 1000UL)

#else  // ---------------------------------------------------------- firmware

#include <Arduino.h>
#define OT_MILLIS() millis()
#define OT_MICROS() micros()

#endif

namespace ot {

template <typename T>
constexpr T clampValue(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Case-insensitive comparison used everywhere enums are parsed from JSON.
inline bool strEqualsI(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 32);
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

inline void copyString(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = 0;
    while (src[n] && n + 1 < dstSize) {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

}  // namespace ot
