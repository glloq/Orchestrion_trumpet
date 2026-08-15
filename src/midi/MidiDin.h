// ============================================================================
//  MidiDin.h - classic 5 pin DIN MIDI over a hardware UART.
//
//  31250 baud, 8N1.  The MIDI IN must be opto-isolated on the hardware side
//  (see docs/HARDWARE.md for the 6N138 circuit); the firmware only sees a
//  clean UART.
//
//  THRU is implemented in software: every byte received is echoed on TX with
//  no interpretation, which is what a hardware THRU port does.
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"
#include "midi/MidiParser.h"

#if !defined(OT_HOST_BUILD)
#include <HardwareSerial.h>
#endif

namespace ot {

class MidiDinTransport final : public IMidiTransport {
public:
    void configure(const MidiDinConfig& cfg);

    bool begin() override;
    void end() override;
    void poll() override;
    void onMidi(const MidiMessage& msg) override;

    bool isConnected() const override { return started_; }
    MidiPort port() const override { return MidiPort::DIN; }
    bool inputEnabled() const override { return cfg_.inEnabled; }
    bool outputEnabled() const override { return cfg_.outEnabled; }

    uint32_t bytesReceived() const { return rxBytes_; }
    uint32_t bytesSent() const { return txBytes_; }

private:
    MidiDinConfig cfg_;
    MidiParser parser_{MidiPort::DIN};
    bool started_ = false;
    uint32_t rxBytes_ = 0;
    uint32_t txBytes_ = 0;
#if !defined(OT_HOST_BUILD)
    HardwareSerial* uart_ = nullptr;
#endif
};

}  // namespace ot
