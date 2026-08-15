// ============================================================================
//  MidiRouter.h - the only place that knows how messages travel.
//
//  Sources : USB, BLE, RTP, DIN, WEB
//  Targets : SOUND_ENGINE, VALVE_ENGINE, USB, BLE, RTP, DIN, MONITOR
//
//  Each route carries its own channel filter, transpose, velocity curve and
//  note range.  The router is pure logic: it is compiled and unit tested on
//  the host.
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"

namespace ot {

struct MidiRouterStats {
    uint32_t rxTotal = 0;
    uint32_t txTotal = 0;
    uint32_t rxPerPort[kMidiPortCount] = {0};
    uint32_t txPerPort[kMidiPortCount] = {0};
    uint32_t droppedByFilter = 0;
    uint32_t loopsSuppressed = 0;
};

class MidiRouter final : public IMidiSink {
public:
    void configure(const MidiConfig& cfg);
    const MidiConfig& config() const { return cfg_; }

    // Registers a destination.  Transports register themselves for their own
    // port; the engines register for SOUND_ENGINE / VALVE_ENGINE / MONITOR.
    void setSink(MidiPort port, IMidiSink* sink);
    void setTransport(MidiPort port, IMidiTransport* transport);
    IMidiTransport* transport(MidiPort port) const;

    // Entry point used by every transport.
    void onMidi(const MidiMessage& msg) override;

    // Emits a message that the firmware itself generated (panic, web UI
    // pass-through) as if it came from `source`.
    void inject(const MidiMessage& msg, MidiPort source);

    // Sends All Sound Off / All Notes Off / Reset Controllers everywhere.
    void broadcastPanic();

    const MidiRouterStats& stats() const { return stats_; }
    void resetStats() { stats_ = MidiRouterStats(); }

    // Builds the default full-duplex routing used on a fresh install.
    static void makeDefaultRoutes(MidiConfig& cfg);

    // Exposed for the unit tests and the web UI preview.
    static uint8_t applyVelocityCurve(VelocityCurve curve, uint8_t velocity,
                                      uint8_t fixedVelocity);

private:
    bool passesFilters(const MidiRoute& route, const MidiMessage& msg,
                       MidiMessage& transformed) const;
    void sendTo(MidiPort dest, const MidiMessage& msg);

    MidiConfig cfg_;
    IMidiSink* sinks_[kMidiPortCount] = {nullptr};
    IMidiTransport* transports_[kMidiPortCount] = {nullptr};
    MidiRouterStats stats_;
    bool reentrant_ = false;
};

}  // namespace ot
