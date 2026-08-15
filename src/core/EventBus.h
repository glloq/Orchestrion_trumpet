// ============================================================================
//  EventBus.h - small fan-out for things that happen once in a while.
//
//  Used for state changes, faults and telemetry pushes: never for audio or
//  MIDI data, which travel through ring buffers instead.  Subscribers are a
//  fixed array of function pointers, so publishing allocates nothing and can
//  be called from any task.
// ============================================================================
#pragma once

#include "core/SystemState.h"

namespace ot {

enum class EventType : uint8_t {
    ModeChanged = 0,
    FaultRaised,
    FaultCleared,
    ConfigSaved,
    ValveStateChanged,
    NoteChanged,
    AudioUnderrun,
    NetworkChanged,
    PanicRequested,
    COUNT
};

struct Event {
    EventType type = EventType::ModeChanged;
    uint32_t a = 0;
    uint32_t b = 0;
    const char* text = nullptr;
};

using EventHandler = void (*)(const Event&, void* context);

class EventBus {
public:
    static EventBus& instance();

    bool subscribe(EventHandler handler, void* context);
    void unsubscribe(EventHandler handler, void* context);
    void publish(const Event& event);
    void publish(EventType type, uint32_t a = 0, uint32_t b = 0, const char* text = nullptr);

private:
    static constexpr uint8_t kMaxSubscribers = 8;
    struct Slot {
        EventHandler handler = nullptr;
        void* context = nullptr;
    };
    Slot slots_[kMaxSubscribers];
};

}  // namespace ot
