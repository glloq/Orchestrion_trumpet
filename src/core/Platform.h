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
#include <mutex>
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
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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

// Integer square root.  Used by the solenoid thermal integral and by the servo
// travel-time estimate, neither of which wants <cmath> pulled into a task that
// must stay predictable.
inline uint32_t isqrt32(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

// Hands the CPU over for a moment. Only ever called from a control task that
// is waiting for the audio task to pick something up; the host build has no
// tasks, so there is nothing to wait for.
#if defined(OT_HOST_BUILD)
inline void sleepMs(uint32_t) {}
#else
inline void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
#endif

// Mutual exclusion between two *non real-time* producers - the network task and
// the application task both publishing a voicing, for instance.  It must never
// appear on the audio path: the whole point of the lock-free mailboxes is that
// the audio task never waits for anyone.
#if defined(OT_HOST_BUILD)
class Mutex {
public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }

private:
    std::mutex m_;
};
#else
class Mutex {
public:
    Mutex() : handle_(xSemaphoreCreateMutex()) {}
    void lock() {
        if (handle_) xSemaphoreTake(handle_, portMAX_DELAY);
    }
    void unlock() {
        if (handle_) xSemaphoreGive(handle_);
    }

private:
    SemaphoreHandle_t handle_;
};
#endif

class MutexLock {
public:
    explicit MutexLock(Mutex& m) : m_(m) { m_.lock(); }
    ~MutexLock() { m_.unlock(); }
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

private:
    Mutex& m_;
};

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
