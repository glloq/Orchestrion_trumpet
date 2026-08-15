// ============================================================================
//  MidiUsb.h - class compliant USB-MIDI, ESP32-S3 only.
//
//  Uses the TinyUSB MIDI class shipped with Arduino-ESP32 3.x, so the trumpet
//  enumerates as a standard USB-MIDI device on Windows, macOS, Linux and a
//  Raspberry Pi with no driver at all.
//
//  On a board without native USB the transport refuses to start and the web UI
//  hides the option entirely (BoardCaps::hasNativeUsb).  A USB *serial* MIDI
//  bridge is a different thing and is never presented as class compliant
//  USB-MIDI - see docs/MIDI.md.
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"

namespace ot {

class MidiUsbTransport final : public IMidiTransport {
public:
    void configure(const MidiUsbConfig& cfg);

    bool begin() override;
    void end() override;
    void poll() override;
    void onMidi(const MidiMessage& msg) override;

    bool isConnected() const override;
    MidiPort port() const override { return MidiPort::USB; }
    bool inputEnabled() const override { return cfg_.inEnabled && available_; }
    bool outputEnabled() const override { return cfg_.outEnabled && available_; }

    // True when the firmware was built for a chip with native USB.
    static bool platformSupportsUsbMidi();

private:
    MidiUsbConfig cfg_;
    bool started_ = false;
    bool available_ = false;
};

}  // namespace ot
