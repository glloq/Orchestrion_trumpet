// ============================================================================
//  MidiWebSocket.h - MIDI produced by the web UI keyboard.
//
//  This is an ordinary transport, exactly like BLE or DIN: the router does not
//  know that the notes come from a browser.  The WebSocket plumbing itself
//  lives in network/WebSocketServer; this class only converts between the JSON
//  events and MidiMessage, and buffers what has to travel back to the browser.
// ============================================================================
#pragma once

#include "core/RingBuffer.h"
#include "midi/IMidiTransport.h"

namespace ot {

class MidiWebSocketTransport final : public IMidiTransport {
public:
    void configure(const MidiWebConfig& cfg);

    bool begin() override;
    void end() override;
    void poll() override {}
    void onMidi(const MidiMessage& msg) override;

    bool isConnected() const override { return clientCount_ > 0; }
    MidiPort port() const override { return MidiPort::WEB; }
    bool inputEnabled() const override { return cfg_.inEnabled; }
    bool outputEnabled() const override { return cfg_.monitorEnabled; }

    // Called by the WebSocket server when a browser sends a MIDI event.
    void injectFromWeb(const MidiMessage& msg);
    void setClientCount(uint8_t count) { clientCount_ = count; }

    // Drained by the network task and pushed to the browsers.
    bool popOutgoing(MidiMessage& out) { return outgoing_.pop(out); }

private:
    MidiWebConfig cfg_;
    bool started_ = false;
    uint8_t clientCount_ = 0;
    RingBuffer<MidiMessage, 64> outgoing_;
};

}  // namespace ot
