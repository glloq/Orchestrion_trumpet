// ============================================================================
//  IMidiTransport.h - contract every MIDI interface obeys.
//
//  BLE, USB, DIN, RTP and the WebSocket bridge are interchangeable from the
//  router's point of view: this is what lets a user swap BLE for DIN without
//  touching the sound engine.
// ============================================================================
#pragma once

#include "midi/MidiMessage.h"

namespace ot {

// Anything that can receive a MIDI message: a transport output, the sound
// engine, the valve engine or the monitor.
class IMidiSink {
public:
    virtual ~IMidiSink() = default;
    virtual void onMidi(const MidiMessage& msg) = 0;
};

class IMidiTransport : public IMidiSink {
public:
    ~IMidiTransport() override = default;

    virtual bool begin() = 0;
    virtual void end() {}
    // Called from the MIDI task; must never block.
    virtual void poll() {}
    virtual bool isConnected() const = 0;
    virtual MidiPort port() const = 0;
    virtual bool inputEnabled() const = 0;
    virtual bool outputEnabled() const = 0;

    // Set by the router at registration time; the transport calls it for
    // every message it decodes from the wire.
    void setReceiver(IMidiSink* sink) { receiver_ = sink; }

protected:
    void deliver(const MidiMessage& msg) {
        if (receiver_) receiver_->onMidi(msg);
    }

private:
    IMidiSink* receiver_ = nullptr;
};

}  // namespace ot
