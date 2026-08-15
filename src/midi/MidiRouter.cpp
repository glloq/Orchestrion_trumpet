#include "midi/MidiRouter.h"

namespace ot {

namespace {
constexpr uint8_t kIdx(MidiPort p) { return static_cast<uint8_t>(p); }

bool isSourcePort(MidiPort p) {
    switch (p) {
        case MidiPort::USB:
        case MidiPort::BLE:
        case MidiPort::RTP:
        case MidiPort::DIN:
        case MidiPort::WEB:
            return true;
        default:
            return false;
    }
}
}  // namespace

void MidiRouter::configure(const MidiConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.routeCount == 0) makeDefaultRoutes(cfg_);
}

void MidiRouter::setSink(MidiPort port, IMidiSink* sink) {
    if (kIdx(port) < kMidiPortCount) sinks_[kIdx(port)] = sink;
}

void MidiRouter::setTransport(MidiPort port, IMidiTransport* transport) {
    if (kIdx(port) >= kMidiPortCount) return;
    transports_[kIdx(port)] = transport;
    sinks_[kIdx(port)] = transport;
    if (transport) transport->setReceiver(this);
}

IMidiTransport* MidiRouter::transport(MidiPort port) const {
    return kIdx(port) < kMidiPortCount ? transports_[kIdx(port)] : nullptr;
}

uint8_t MidiRouter::applyVelocityCurve(VelocityCurve curve, uint8_t velocity,
                                       uint8_t fixedVelocity) {
    if (velocity == 0) return 0;  // never turn a note-off into a note-on
    switch (curve) {
        case VelocityCurve::SOFT: {
            // Compressive: easier to reach loud dynamics from a light keyboard.
            float v = velocity / 127.0f;
            v = v * (2.0f - v);
            return static_cast<uint8_t>(clampValue<int>(static_cast<int>(v * 127.0f + 0.5f), 1, 127));
        }
        case VelocityCurve::HARD: {
            float v = velocity / 127.0f;
            v = v * v;
            return static_cast<uint8_t>(clampValue<int>(static_cast<int>(v * 127.0f + 0.5f), 1, 127));
        }
        case VelocityCurve::FIXED:
            return fixedVelocity == 0 ? 1 : fixedVelocity;
        case VelocityCurve::LINEAR:
        default:
            return velocity;
    }
}

bool MidiRouter::passesFilters(const MidiRoute& route, const MidiMessage& msg,
                               MidiMessage& transformed) const {
    transformed = msg;

    if (msg.isRealtime() || !msg.isChannelMessage()) {
        // Real-time and system messages bypass the note/velocity filters but
        // still honour the enable flag of the route.
        return true;
    }

    const uint16_t channelBit = static_cast<uint16_t>(1u << ((msg.channel - 1) & 0x0F));
    if ((cfg_.globalChannelMask & channelBit) == 0) return false;
    if ((route.channelMask & channelBit) == 0) return false;

    const bool isNote = msg.type == MidiType::NoteOn || msg.type == MidiType::NoteOff ||
                        msg.type == MidiType::PolyPressure;
    if (isNote) {
        int note = static_cast<int>(msg.data1) + route.transpose;
        if (note < 0 || note > 127) return false;
        if (msg.data1 < route.noteMin || msg.data1 > route.noteMax) return false;
        transformed.data1 = static_cast<uint8_t>(note);

        if (msg.type == MidiType::NoteOn) {
            transformed.data2 =
                applyVelocityCurve(route.velocityCurve, msg.data2, route.fixedVelocity);
        }
    }
    return true;
}

void MidiRouter::sendTo(MidiPort dest, const MidiMessage& msg) {
    IMidiSink* sink = sinks_[kIdx(dest)];
    if (!sink) return;
    IMidiTransport* t = transports_[kIdx(dest)];
    if (t && !t->outputEnabled()) return;
    sink->onMidi(msg);
    ++stats_.txTotal;
    ++stats_.txPerPort[kIdx(dest)];
}

void MidiRouter::onMidi(const MidiMessage& msg) {
    if (!isSourcePort(msg.source)) return;

    ++stats_.rxTotal;
    ++stats_.rxPerPort[kIdx(msg.source)];

    // Single-hop routing plus this guard: a destination transport can never
    // re-enter the router while we are dispatching, so no MIDI loop can build
    // up inside the firmware.
    if (reentrant_) {
        ++stats_.loopsSuppressed;
        return;
    }
    reentrant_ = true;

    // The monitor sees the raw stream, before any transformation.
    if (cfg_.web.monitorEnabled && sinks_[kIdx(MidiPort::MONITOR)]) {
        sinks_[kIdx(MidiPort::MONITOR)]->onMidi(msg);
    }

    bool anyRoute = false;
    for (uint8_t i = 0; i < cfg_.routeCount && i < kMaxRoutes; ++i) {
        const MidiRoute& r = cfg_.routes[i];
        if (!r.enabled || r.source != msg.source) continue;
        if (r.destination == MidiPort::NONE || r.destination == MidiPort::MONITOR) continue;
        if (cfg_.suppressLoops && r.destination == msg.source) {
            ++stats_.loopsSuppressed;
            continue;
        }
        anyRoute = true;

        MidiMessage out;
        if (!passesFilters(r, msg, out)) {
            ++stats_.droppedByFilter;
            continue;
        }
        sendTo(r.destination, out);
    }
    if (!anyRoute) ++stats_.droppedByFilter;

    reentrant_ = false;
}

void MidiRouter::inject(const MidiMessage& msg, MidiPort source) {
    MidiMessage m = msg;
    m.source = source;
    if (m.timestampMs == 0) m.timestampMs = OT_MILLIS();
    onMidi(m);
}

void MidiRouter::broadcastPanic() {
    // Sent to the engines and to every enabled output so an external
    // synthesiser chained behind the trumpet also stops.
    const MidiPort targets[] = {MidiPort::SOUND_ENGINE, MidiPort::VALVE_ENGINE, MidiPort::USB,
                                MidiPort::BLE,          MidiPort::RTP,          MidiPort::DIN};
    const uint8_t controllers[] = {cc::AllSoundOff, cc::ResetControllers, cc::AllNotesOff};

    for (MidiPort dest : targets) {
        for (uint8_t ctrl : controllers) {
            MidiMessage m = MidiMessage::controlChange(cfg_.outputChannel, ctrl, 0);
            m.source = MidiPort::NONE;
            m.timestampMs = OT_MILLIS();
            sendTo(dest, m);
        }
    }
}

void MidiRouter::makeDefaultRoutes(MidiConfig& cfg) {
    cfg.routeCount = 0;
    const MidiPort sources[] = {MidiPort::USB, MidiPort::BLE, MidiPort::RTP, MidiPort::DIN,
                                MidiPort::WEB};
    // Out of the box every input drives both engines: plug anything in and the
    // trumpet plays.  Hardware outputs stay off so no unexpected MIDI loop can
    // form with a DAW.
    for (MidiPort src : sources) {
        for (MidiPort dst : {MidiPort::SOUND_ENGINE, MidiPort::VALVE_ENGINE}) {
            if (cfg.routeCount >= kMaxRoutes) return;
            MidiRoute& r = cfg.routes[cfg.routeCount++];
            r = MidiRoute();
            r.source = src;
            r.destination = dst;
            r.enabled = true;
        }
    }
}

}  // namespace ot
