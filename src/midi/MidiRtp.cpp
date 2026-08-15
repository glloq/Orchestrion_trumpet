#include "midi/MidiRtp.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

namespace ot {

namespace {
constexpr uint16_t kSignature = 0xFFFF;
constexpr uint16_t kCmdInvitation = 0x494E;          // 'IN'
constexpr uint16_t kCmdInvitationAccepted = 0x4F4B;  // 'OK'
constexpr uint16_t kCmdInvitationRejected = 0x4E4F;  // 'NO'
constexpr uint16_t kCmdEndSession = 0x4259;          // 'BY'
constexpr uint16_t kCmdSynchronisation = 0x434B;     // 'CK'
constexpr uint16_t kCmdReceiverFeedback = 0x5253;    // 'RS'
constexpr uint8_t kRtpPayloadType = 0x61;            // 97, dynamic, RTP-MIDI
constexpr uint32_t kProtocolVersion = 2;

#if !defined(OT_HOST_BUILD)
uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t readU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
void writeU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
void writeU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}
// AppleMIDI timestamps count in units of 100 microseconds.
uint64_t rtpNow() { return static_cast<uint64_t>(micros()) / 10ULL; }
#endif
}  // namespace

void MidiRtpTransport::configure(const MidiRtpConfig& cfg) { cfg_ = cfg; }

bool MidiRtpTransport::begin() {
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (started_) return true;
    if (!cfg_.inEnabled && !cfg_.outEnabled) return false;
    if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0 &&
        WiFi.getMode() == WIFI_OFF) {
        OT_LOGW("rtp", "no network yet, RTP-MIDI will start when Wi-Fi is up");
        return false;
    }

    ssrc_ = esp_random();
    if (!control_.begin(cfg_.controlPort)) {
        OT_LOGE("rtp", "cannot bind UDP %u", cfg_.controlPort);
        return false;
    }
    if (!data_.begin(static_cast<uint16_t>(cfg_.controlPort + 1))) {
        control_.stop();
        OT_LOGE("rtp", "cannot bind UDP %u", cfg_.controlPort + 1);
        return false;
    }
    parser_.reset();
    started_ = true;
    OT_LOGI("rtp", "RTP-MIDI session \"%s\" listening on %u/%u", cfg_.sessionName,
            cfg_.controlPort, cfg_.controlPort + 1);
    return true;
#endif
}

void MidiRtpTransport::end() {
#if !defined(OT_HOST_BUILD)
    if (started_) {
        control_.stop();
        data_.stop();
    }
#endif
    started_ = false;
    sessionActive_ = false;
    controlAccepted_ = false;
}

#if !defined(OT_HOST_BUILD)

void MidiRtpTransport::sendExchange(WiFiUDP& socket, const IPAddress& ip, uint16_t port,
                                    uint16_t command, uint32_t initiatorToken) {
    uint8_t packet[16 + kNameLen + 1];
    writeU16(packet + 0, kSignature);
    writeU16(packet + 2, command);
    writeU32(packet + 4, kProtocolVersion);
    writeU32(packet + 8, initiatorToken);
    writeU32(packet + 12, ssrc_);
    size_t length = 16;
    if (command == kCmdInvitationAccepted || command == kCmdInvitationRejected) {
        const char* name = cfg_.sessionName[0] ? cfg_.sessionName : "MIDI Trumpet";
        size_t n = 0;
        while (name[n] && n < kNameLen) {
            packet[length + n] = static_cast<uint8_t>(name[n]);
            ++n;
        }
        packet[length + n] = 0;
        length += n + 1;
    }
    socket.beginPacket(ip, port);
    socket.write(packet, length);
    socket.endPacket();
}

void MidiRtpTransport::sendClockResponse(const uint8_t* request, const IPAddress& ip,
                                         uint16_t port) {
    // CK0 (initiator) -> CK1 (us) -> CK2 (initiator).  We only ever answer,
    // never initiate, so a single reply is enough.
    uint8_t packet[36];
    memcpy(packet, request, 36);
    writeU16(packet + 0, kSignature);
    writeU16(packet + 2, kCmdSynchronisation);
    writeU32(packet + 4, ssrc_);
    const uint8_t count = request[8];
    if (count != 0) return;   // CK1/CK2 need no answer from a responder
    packet[8] = 1;
    const uint64_t now = rtpNow();
    writeU32(packet + 20, static_cast<uint32_t>(now >> 32));
    writeU32(packet + 24, static_cast<uint32_t>(now & 0xFFFFFFFFULL));
    data_.beginPacket(ip, port);
    data_.write(packet, sizeof(packet));
    data_.endPacket();
}

void MidiRtpTransport::handleExchange(WiFiUDP& socket, const uint8_t* data, size_t length,
                                      bool isControlPort) {
    if (length < 4) return;
    const uint16_t command = readU16(data + 2);
    const IPAddress remoteIp = socket.remoteIP();
    const uint16_t remotePort = socket.remotePort();

    switch (command) {
        case kCmdInvitation: {
            if (length < 16) return;
            const uint32_t token = readU32(data + 8);
            const uint32_t remoteSsrc = readU32(data + 12);

            // Only one session at a time: a second initiator is politely
            // refused instead of silently stealing the first one's stream.
            if (sessionActive_ && remoteSsrc != peerSsrc_) {
                sendExchange(socket, remoteIp, remotePort, kCmdInvitationRejected, token);
                return;
            }

            if (isControlPort) {
                peerIp_ = remoteIp;
                peerControlPort_ = remotePort;
                peerSsrc_ = remoteSsrc;
                controlAccepted_ = true;
                if (length > 16) {
                    copyString(peerName_, sizeof(peerName_),
                               reinterpret_cast<const char*>(data + 16));
                }
            } else {
                peerDataPort_ = remotePort;
                sessionActive_ = controlAccepted_;
                if (sessionActive_) {
                    OT_LOGI("rtp", "session opened with \"%s\"", peerName_);
                }
            }
            sendExchange(socket, remoteIp, remotePort, kCmdInvitationAccepted, token);
            break;
        }

        case kCmdEndSession:
            if (length >= 16 && readU32(data + 12) == peerSsrc_) {
                OT_LOGI("rtp", "session closed by \"%s\"", peerName_);
                sessionActive_ = false;
                controlAccepted_ = false;
                peerName_[0] = '\0';
                parser_.reset();
            }
            break;

        case kCmdSynchronisation:
            if (length >= 36 && !isControlPort) sendClockResponse(data, remoteIp, remotePort);
            break;

        case kCmdReceiverFeedback:
            // Nothing to do: we do not implement the recovery journal, so
            // there is no history to trim.
            break;

        default:
            break;
    }
}

void MidiRtpTransport::handleRtpPayload(const uint8_t* data, size_t length) {
    if (length < 13) return;
    if ((data[0] & 0xC0) != 0x80) return;                 // RTP version 2
    if ((data[1] & 0x7F) != kRtpPayloadType) return;      // RTP-MIDI

    size_t offset = 12;
    const uint8_t csrcCount = data[0] & 0x0F;
    offset += csrcCount * 4u;
    if (offset >= length) return;

    // MIDI command section header.
    const uint8_t header = data[offset++];
    const bool longHeader = (header & 0x80) != 0;
    const bool hasDeltaFirst = (header & 0x20) != 0;   // Z flag
    size_t listLength = header & 0x0F;
    if (longHeader) {
        if (offset >= length) return;
        listLength = (static_cast<size_t>(header & 0x0F) << 8) | data[offset++];
    }
    if (offset + listLength > length) listLength = length - offset;

    size_t index = 0;
    if (hasDeltaFirst) {
        // Skip the leading delta time (variable length quantity).
        while (index < listLength && (data[offset + index] & 0x80)) ++index;
        if (index < listLength) ++index;
    }

    while (index < listLength) {
        const uint8_t byte = data[offset + index++];
        MidiMessage msg;
        if (parser_.parse(byte, msg)) {
            deliver(msg);
            // Every following command is preceded by its own delta time.
            while (index < listLength && (data[offset + index] & 0x80)) ++index;
            if (index < listLength) ++index;
        }
    }
}

void MidiRtpTransport::pollControl() {
    uint8_t buffer[128];
    int size = control_.parsePacket();
    while (size > 0) {
        const int n = control_.read(buffer, sizeof(buffer));
        if (n >= 4 && readU16(buffer) == kSignature) {
            handleExchange(control_, buffer, static_cast<size_t>(n), true);
        }
        size = control_.parsePacket();
    }
}

void MidiRtpTransport::pollData() {
    uint8_t buffer[256];
    int size = data_.parsePacket();
    uint8_t budget = 8;   // bounded work per poll
    while (size > 0 && budget--) {
        const int n = data_.read(buffer, sizeof(buffer));
        if (n >= 4) {
            if (readU16(buffer) == kSignature) {
                handleExchange(data_, buffer, static_cast<size_t>(n), false);
            } else if (cfg_.inEnabled && sessionActive_) {
                handleRtpPayload(buffer, static_cast<size_t>(n));
            }
        }
        size = data_.parsePacket();
    }
}

void MidiRtpTransport::flushOutput() {
    if (txLength_ == 0) return;
    if (!sessionActive_ || !cfg_.outEnabled) {
        txLength_ = 0;
        return;
    }

    uint8_t packet[12 + 2 + sizeof(txBuffer_)];
    packet[0] = 0x80;                 // V=2, no padding, no extension, CC=0
    packet[1] = kRtpPayloadType;      // M=0
    writeU16(packet + 2, ++sequence_);
    writeU32(packet + 4, static_cast<uint32_t>(rtpNow()));
    writeU32(packet + 8, ssrc_);

    size_t offset = 12;
    if (txLength_ < 16) {
        packet[offset++] = static_cast<uint8_t>(txLength_);            // short header
    } else {
        packet[offset++] = static_cast<uint8_t>(0x80 | (txLength_ >> 8));
        packet[offset++] = static_cast<uint8_t>(txLength_ & 0xFF);
    }
    memcpy(packet + offset, txBuffer_, txLength_);
    offset += txLength_;

    data_.beginPacket(peerIp_, peerDataPort_);
    data_.write(packet, offset);
    data_.endPacket();
    txLength_ = 0;
}

void MidiRtpTransport::poll() {
    if (!started_) return;
    pollControl();
    pollData();
    flushOutput();
}

void MidiRtpTransport::onMidi(const MidiMessage& msg) {
    if (!started_ || !sessionActive_ || !cfg_.outEnabled) return;
    uint8_t bytes[3];
    const uint8_t n = msg.toBytes(bytes);
    if (n == 0) return;
    // One extra byte per following command for its delta time.
    const uint8_t need = static_cast<uint8_t>(n + (txLength_ ? 1 : 0));
    if (txLength_ + need > sizeof(txBuffer_)) flushOutput();
    if (txLength_ > 0) txBuffer_[txLength_++] = 0;   // delta time = 0
    for (uint8_t i = 0; i < n; ++i) txBuffer_[txLength_++] = bytes[i];
}

#else  // ------------------------------------------------------- host build

void MidiRtpTransport::poll() {}
void MidiRtpTransport::onMidi(const MidiMessage&) {}

#endif

}  // namespace ot
