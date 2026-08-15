// ============================================================================
//  MidiBle.h - BLE MIDI (Apple / MMA "MIDI over Bluetooth LE" profile).
//
//  Implemented directly on the Arduino-ESP32 BLE stack rather than through a
//  third party library: the profile is one service with one characteristic and
//  a five byte timestamp header, and owning it means the project does not
//  break when an external library changes.
//
//    Service        03B80E5A-EDE8-4B33-A751-6CE34EC4C700
//    Characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3  (write | notify)
//
//  Works with macOS, iOS, Android, and Windows via a BLE-MIDI bridge.
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"
#include "midi/MidiParser.h"

namespace ot {

class MidiBleTransport final : public IMidiTransport {
public:
    void configure(const MidiBleConfig& cfg);

    bool begin() override;
    void end() override;
    void poll() override;
    void onMidi(const MidiMessage& msg) override;

    bool isConnected() const override { return connected_; }
    MidiPort port() const override { return MidiPort::BLE; }
    bool inputEnabled() const override { return cfg_.inEnabled; }
    bool outputEnabled() const override { return cfg_.outEnabled; }

    // Called by the BLE callbacks (internal).
    void handleIncoming(const uint8_t* data, size_t length);
    void setConnected(bool state);

private:
    void flushOutput();

    MidiBleConfig cfg_;
    MidiParser parser_{MidiPort::BLE};
    bool started_ = false;
    volatile bool connected_ = false;

    // Outgoing packets are accumulated and flushed once per poll so a burst of
    // notes becomes one BLE notification instead of one per message.
    uint8_t txBuffer_[20];
    uint8_t txLength_ = 0;
    uint8_t txRunningStatus_ = 0;
};

}  // namespace ot
