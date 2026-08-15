// ============================================================================
//  MidiRtp.h - RTP-MIDI / AppleMIDI over Wi-Fi.
//
//  Implements the session protocol directly on two UDP sockets rather than
//  depending on an external library, for the same reason as BLE MIDI: the
//  protocol is small and owning it keeps the build reproducible.
//
//    control port  (default 5004)  invitation, end of session
//    data port     (control + 1)   clock synchronisation, MIDI payload
//
//  The instrument is always the *responder*: a DAW, an rtpMIDI driver on
//  Windows or the macOS Audio MIDI Setup invites it.  One session at a time is
//  supported, which is what a monophonic instrument needs.
//
//  This transport is completely independent of the web server: the WebSocket
//  bridge is a separate transport (MidiWebSocket).
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"
#include "midi/MidiParser.h"

#if !defined(OT_HOST_BUILD)
#include <WiFiUdp.h>
#endif

namespace ot {

class MidiRtpTransport final : public IMidiTransport {
public:
    void configure(const MidiRtpConfig& cfg);

    bool begin() override;
    void end() override;
    void poll() override;
    void onMidi(const MidiMessage& msg) override;

    bool isConnected() const override { return sessionActive_; }
    MidiPort port() const override { return MidiPort::RTP; }
    bool inputEnabled() const override { return cfg_.inEnabled; }
    bool outputEnabled() const override { return cfg_.outEnabled; }

    const char* peerName() const { return peerName_; }
    uint32_t ssrc() const { return ssrc_; }

private:
#if !defined(OT_HOST_BUILD)
    void pollControl();
    void pollData();
    void handleExchange(WiFiUDP& socket, const uint8_t* data, size_t length, bool isControlPort);
    void handleRtpPayload(const uint8_t* data, size_t length);
    void sendExchange(WiFiUDP& socket, const IPAddress& ip, uint16_t port, uint16_t command,
                      uint32_t initiatorToken);
    void sendClockResponse(const uint8_t* request, const IPAddress& ip, uint16_t port);
    void flushOutput();

    WiFiUDP control_;
    WiFiUDP data_;
    IPAddress peerIp_;
    uint16_t peerControlPort_ = 0;
    uint16_t peerDataPort_ = 0;
#endif

    MidiRtpConfig cfg_;
    MidiParser parser_{MidiPort::RTP};
    char peerName_[kNameLen] = "";
    uint32_t ssrc_ = 0;
    uint32_t peerSsrc_ = 0;
    uint16_t sequence_ = 0;
    bool started_ = false;
    bool sessionActive_ = false;
    bool controlAccepted_ = false;

    uint8_t txBuffer_[64];
    uint8_t txLength_ = 0;
};

}  // namespace ot
