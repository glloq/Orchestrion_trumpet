// ============================================================================
//  RingBuffer.h - single producer / single consumer queue.
//
//  Used to hand MIDI messages from the MIDI task to the audio task without a
//  mutex: the audio task must never block, and a priority inversion on a lock
//  is exactly how an I2S underrun happens.
//
//  Capacity must be a power of two.  Storage is a fixed member: no allocation
//  ever happens on the real-time path.
// ============================================================================
#pragma once

#include "core/Platform.h"

#if !defined(OT_HOST_BUILD)
#include <atomic>
#else
#include <atomic>
#endif

namespace ot {

template <typename T, uint16_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    // Producer side.
    bool push(const T& item) {
        const uint16_t head = head_.load(std::memory_order_relaxed);
        const uint16_t next = static_cast<uint16_t>((head + 1) & kMask);
        if (next == tail_.load(std::memory_order_acquire)) {
            ++dropped_;
            return false;
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side.
    bool pop(T& out) {
        const uint16_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = buffer_[tail];
        tail_.store(static_cast<uint16_t>((tail + 1) & kMask), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    uint16_t size() const {
        const uint16_t h = head_.load(std::memory_order_acquire);
        const uint16_t t = tail_.load(std::memory_order_acquire);
        return static_cast<uint16_t>((h - t) & kMask);
    }

    uint32_t dropped() const { return dropped_; }
    void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    static constexpr uint16_t kMask = Capacity - 1;
    T buffer_[Capacity];
    std::atomic<uint16_t> head_{0};
    std::atomic<uint16_t> tail_{0};
    uint32_t dropped_ = 0;
};

}  // namespace ot
