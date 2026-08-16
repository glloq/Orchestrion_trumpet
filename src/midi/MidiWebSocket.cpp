#include "midi/MidiWebSocket.h"

namespace ot {

void MidiWebSocketTransport::configure(const MidiWebConfig& cfg) { cfg_ = cfg; }

bool MidiWebSocketTransport::begin() {
    started_ = true;
    return true;
}

void MidiWebSocketTransport::end() {
    started_ = false;
    clientCount_ = 0;
}

void MidiWebSocketTransport::injectFromWeb(const MidiMessage& msg) {
    if (!started_ || !cfg_.inEnabled) return;
    MidiMessage m = msg;
    m.source = MidiPort::WEB;
    if (m.timestampMs == 0) m.timestampMs = OT_MILLIS();
    // Queue only - the delivery happens on the MIDI task, in poll().
    incoming_.push(m);
}

void MidiWebSocketTransport::poll() {
    if (!started_) return;
    MidiMessage m;
    // Bounded like the DIN reader: a browser holding the keyboard down must
    // never be able to starve the other transports.
    uint8_t guard = 0;
    while (guard++ < 32 && incoming_.pop(m)) {
        if (!cfg_.inEnabled) continue;   // switched off while queued
        deliver(m);
    }
}

void MidiWebSocketTransport::onMidi(const MidiMessage& msg) {
    if (!started_ || clientCount_ == 0) return;
    // Never block the router: if the browsers cannot keep up the message is
    // dropped rather than delaying the MIDI task.
    outgoing_.push(msg);
}

}  // namespace ot
