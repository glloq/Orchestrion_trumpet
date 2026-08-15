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
    deliver(m);
}

void MidiWebSocketTransport::onMidi(const MidiMessage& msg) {
    if (!started_ || clientCount_ == 0) return;
    // Never block the router: if the browsers cannot keep up the message is
    // dropped rather than delaying the MIDI task.
    outgoing_.push(msg);
}

}  // namespace ot
