#include "midi/MidiParser.h"

namespace ot {

void MidiParser::reset() {
    runningStatus_ = 0;
    pendingCount_ = 0;
    expected_ = 0;
    status_ = 0;
    inSysEx_ = false;
    sysexLength_ = 0;
}

uint8_t MidiParser::dataBytesFor(uint8_t status) {
    switch (status & 0xF0) {
        case 0x80:
        case 0x90:
        case 0xA0:
        case 0xB0:
        case 0xE0:
            return 2;
        case 0xC0:
        case 0xD0:
            return 1;
        case 0xF0:
            switch (status) {
                case 0xF1:
                case 0xF3:
                    return 1;
                case 0xF2:
                    return 2;
                default:
                    return 0;
            }
        default:
            return 0;
    }
}

bool MidiParser::emit(MidiMessage& out) {
    out = MidiMessage();
    out.source = source_;
    out.timestampMs = OT_MILLIS();
    if (status_ < 0xF0) {
        out.type = static_cast<MidiType>(status_ & 0xF0);
        out.channel = static_cast<uint8_t>((status_ & 0x0F) + 1);
    } else {
        out.type = static_cast<MidiType>(status_);
        out.channel = 1;
    }
    out.data1 = pending_[0];
    out.data2 = pending_[1];
    pendingCount_ = 0;
    return true;
}

bool MidiParser::parse(uint8_t byte, MidiMessage& out) {
    // Real-time bytes may appear anywhere, even inside a SysEx block.
    if (byte >= 0xF8) {
        out = MidiMessage();
        out.source = source_;
        out.timestampMs = OT_MILLIS();
        out.type = static_cast<MidiType>(byte);
        return true;
    }

    if (inSysEx_) {
        if (byte == 0xF7) {
            inSysEx_ = false;
            out = MidiMessage();
            out.source = source_;
            out.timestampMs = OT_MILLIS();
            out.type = MidiType::SystemExclusive;
            out.sysex = sysex_;
            out.sysexLength = sysexLength_;
            return true;
        }
        if (byte & 0x80) {
            // Any other status byte aborts the SysEx block.
            inSysEx_ = false;
            sysexLength_ = 0;
        } else {
            if (sysexLength_ < kSysExBufferSize) {
                sysex_[sysexLength_++] = byte;
            } else if (sysexOverflow_ < 0xFFFF) {
                ++sysexOverflow_;
            }
            return false;
        }
    }

    if (byte & 0x80) {
        if (byte == 0xF0) {
            inSysEx_ = true;
            sysexLength_ = 0;
            pendingCount_ = 0;
            runningStatus_ = 0;
            return false;
        }
        status_ = byte;
        pendingCount_ = 0;
        expected_ = dataBytesFor(byte);
        // Running status only applies to channel messages.
        runningStatus_ = (byte < 0xF0) ? byte : 0;
        if (expected_ == 0) return emit(out);
        return false;
    }

    // Data byte.
    if (status_ == 0) {
        if (runningStatus_ == 0) return false;  // orphan data, ignore
        status_ = runningStatus_;
        expected_ = dataBytesFor(status_);
    }
    if (pendingCount_ < 2) pending_[pendingCount_] = byte;
    ++pendingCount_;
    if (pendingCount_ >= expected_) {
        bool ok = emit(out);
        // Stay armed for running status on channel messages.
        status_ = runningStatus_;
        expected_ = dataBytesFor(status_);
        return ok;
    }
    return false;
}

}  // namespace ot
