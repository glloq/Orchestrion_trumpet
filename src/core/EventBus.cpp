#include "core/EventBus.h"

namespace ot {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

bool EventBus::subscribe(EventHandler handler, void* context) {
    if (!handler) return false;
    for (auto& slot : slots_) {
        if (slot.handler == nullptr) {
            slot.handler = handler;
            slot.context = context;
            return true;
        }
    }
    return false;
}

void EventBus::unsubscribe(EventHandler handler, void* context) {
    for (auto& slot : slots_) {
        if (slot.handler == handler && slot.context == context) {
            slot.handler = nullptr;
            slot.context = nullptr;
        }
    }
}

void EventBus::publish(const Event& event) {
    for (auto& slot : slots_) {
        if (slot.handler) slot.handler(event, slot.context);
    }
}

void EventBus::publish(EventType type, uint32_t a, uint32_t b, const char* text) {
    Event e;
    e.type = type;
    e.a = a;
    e.b = b;
    e.text = text;
    publish(e);
}

}  // namespace ot
