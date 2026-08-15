// ============================================================================
//  MidiParser.h - byte stream -> MidiMessage.
//
//  Shared by the DIN UART, the USB serial bridge and the RTP payload decoder.
//  Handles running status, interleaved real-time bytes and SysEx.  Allocation
//  free: the SysEx buffer is a fixed member.
// ============================================================================
#pragma once

#include "midi/MidiMessage.h"

namespace ot {

static constexpr uint16_t kSysExBufferSize = 256;

class MidiParser {
public:
    explicit MidiParser(MidiPort source = MidiPort::NONE) : source_(source) {}

    void setSource(MidiPort s) { source_ = s; }
    void reset();

    // Feeds one byte.  Returns true when `out` holds a complete message.
    bool parse(uint8_t byte, MidiMessage& out);

    uint16_t droppedSysExBytes() const { return sysexOverflow_; }

private:
    MidiPort source_;
    uint8_t runningStatus_ = 0;
    uint8_t pending_[2] = {0, 0};
    uint8_t pendingCount_ = 0;
    uint8_t expected_ = 0;
    uint8_t status_ = 0;
    bool inSysEx_ = false;
    uint16_t sysexLength_ = 0;
    uint16_t sysexOverflow_ = 0;
    uint8_t sysex_[kSysExBufferSize];

    static uint8_t dataBytesFor(uint8_t status);
    bool emit(MidiMessage& out);
};

}  // namespace ot
