// ============================================================================
//  LatestValue.h - hand one whole value from a producer to a real-time
//  consumer, without a lock and without ever tearing it.
//
//  A `volatile bool` next to a struct is NOT this.  Copying a VoicingConfig is
//  hundreds of stores; on a dual-core ESP32 the audio task can read the first
//  half of the new value and the second half of the old one, and the result is
//  a sound nobody asked for - or a filter with a coefficient from one voicing
//  and a gain from another.
//
//  Three slots, not two.  With two, the producer has nowhere safe to write
//  while the consumer holds one and a published-but-not-yet-taken value sits in
//  the other; it would have to wait, and "wait" is what a preview slider must
//  never make the audio task do.  With three:
//
//      writeIndex_   owned by the producer, always
//      readIndex_    owned by the consumer, always
//      ready_        the third slot, plus a flag saying it is fresh
//
//  Both sides swap their own index with `ready_` in one atomic exchange, so the
//  three indices are always distinct and each side only ever touches a slot it
//  owns.  Publishing twice without a take simply overwrites the older value:
//  for a tuning slider, the latest value is the only one that matters.
//
//  Both operations are wait-free.  The producer side is safe for ONE producer;
//  two producers must be serialised by a mutex between themselves (see
//  Platform.h), which costs the real-time consumer nothing.
// ============================================================================
#pragma once

#include <atomic>

#include "core/Platform.h"

namespace ot {

template <typename T>
class LatestValue {
public:
    // Producer side.
    void publish(const T& value) {
        slots_[writeIndex_] = value;
        writeIndex_ = static_cast<uint8_t>(
            ready_.exchange(static_cast<uint8_t>(writeIndex_ | kFresh),
                            std::memory_order_release) & kIndexMask);
        ++published_;
    }

    // Consumer side.  False when nothing new has been published.
    bool take(T& out) {
        if ((ready_.load(std::memory_order_acquire) & kFresh) == 0) return false;
        readIndex_ = static_cast<uint8_t>(
            ready_.exchange(readIndex_, std::memory_order_acquire) & kIndexMask);
        out = slots_[readIndex_];
        ++taken_;
        return true;
    }

    bool pending() const { return (ready_.load(std::memory_order_acquire) & kFresh) != 0; }

    // Diagnostics only: how many values were published and how many the
    // consumer actually saw.  A large difference means the producer is moving
    // faster than the audio blocks, which is allowed but worth showing.
    uint32_t published() const { return published_; }
    uint32_t taken() const { return taken_; }

private:
    static constexpr uint8_t kIndexMask = 0x03;
    static constexpr uint8_t kFresh = 0x80;

    T slots_[3];
    uint8_t writeIndex_ = 0;            // producer only
    uint8_t readIndex_ = 1;             // consumer only
    std::atomic<uint8_t> ready_{2};     // the third slot, not fresh yet
    uint32_t published_ = 0;
    uint32_t taken_ = 0;
};

}  // namespace ot
