// ============================================================================
//  MidiMessage.h - the single currency used inside the firmware.
//
//  Every transport converts its wire format into this struct and hands it to
//  the router.  Nothing downstream knows or cares whether a note arrived over
//  BLE, a DIN cable or the web keyboard.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

enum class MidiType : uint8_t {
    Invalid = 0x00,
    NoteOff = 0x80,
    NoteOn = 0x90,
    PolyPressure = 0xA0,
    ControlChange = 0xB0,
    ProgramChange = 0xC0,
    ChannelPressure = 0xD0,
    PitchBend = 0xE0,
    SystemExclusive = 0xF0,
    TimeCode = 0xF1,
    SongPosition = 0xF2,
    SongSelect = 0xF3,
    TuneRequest = 0xF6,
    SysExEnd = 0xF7,
    Clock = 0xF8,
    Start = 0xFA,
    Continue = 0xFB,
    Stop = 0xFC,
    ActiveSensing = 0xFE,
    SystemReset = 0xFF
};

// Controller numbers the instrument reacts to.
namespace cc {
static constexpr uint8_t Modulation = 1;
static constexpr uint8_t Breath = 2;
static constexpr uint8_t Volume = 7;
static constexpr uint8_t Expression = 11;
static constexpr uint8_t Sustain = 64;        // hold pedal, >= 64 is down
static constexpr uint8_t DataEntryMsb = 6;    // RPN / NRPN value
static constexpr uint8_t DataEntryLsb = 38;
static constexpr uint8_t RpnLsb = 100;
static constexpr uint8_t RpnMsb = 101;
static constexpr uint8_t AllSoundOff = 120;
static constexpr uint8_t ResetControllers = 121;
static constexpr uint8_t AllNotesOff = 123;
}  // namespace cc

struct MidiMessage {
    MidiType type = MidiType::Invalid;
    uint8_t channel = 1;   // 1..16
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    MidiPort source = MidiPort::NONE;
    uint32_t timestampMs = 0;

    // SysEx payload lives in a shared pool owned by the transport; the router
    // only forwards the pointer/length pair and never takes ownership.
    const uint8_t* sysex = nullptr;
    uint16_t sysexLength = 0;

    bool isChannelMessage() const {
        return type >= MidiType::NoteOff && type <= MidiType::PitchBend;
    }
    bool isRealtime() const { return static_cast<uint8_t>(type) >= 0xF8; }
    bool isNoteOn() const { return type == MidiType::NoteOn && data2 > 0; }
    bool isNoteOff() const {
        return type == MidiType::NoteOff || (type == MidiType::NoteOn && data2 == 0);
    }

    // 14 bit signed pitch bend, -8192..+8191
    int16_t pitchBendValue() const {
        return static_cast<int16_t>((static_cast<uint16_t>(data2) << 7 | data1)) - 8192;
    }

    static MidiMessage noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        MidiMessage m;
        m.type = MidiType::NoteOn;
        m.channel = channel;
        m.data1 = note;
        m.data2 = velocity;
        return m;
    }
    static MidiMessage noteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0) {
        MidiMessage m;
        m.type = MidiType::NoteOff;
        m.channel = channel;
        m.data1 = note;
        m.data2 = velocity;
        return m;
    }
    static MidiMessage controlChange(uint8_t channel, uint8_t number, uint8_t value) {
        MidiMessage m;
        m.type = MidiType::ControlChange;
        m.channel = channel;
        m.data1 = number;
        m.data2 = value;
        return m;
    }
    static MidiMessage pitchBend(uint8_t channel, int16_t value) {
        MidiMessage m;
        m.type = MidiType::PitchBend;
        m.channel = channel;
        uint16_t v = static_cast<uint16_t>(clampValue<int32_t>(value + 8192, 0, 16383));
        m.data1 = v & 0x7F;
        m.data2 = (v >> 7) & 0x7F;
        return m;
    }

    // Serialises to the DIN/BLE/RTP wire format.  Returns the byte count, 0
    // for messages that have no direct representation (SysEx is sent by the
    // transports themselves).
    uint8_t toBytes(uint8_t* out) const {
        if (type == MidiType::Invalid || type == MidiType::SystemExclusive) return 0;
        uint8_t status = static_cast<uint8_t>(type);
        if (isChannelMessage()) {
            status = static_cast<uint8_t>(status | ((channel - 1) & 0x0F));
        }
        out[0] = status;
        switch (type) {
            case MidiType::ProgramChange:
            case MidiType::ChannelPressure:
            case MidiType::TimeCode:
            case MidiType::SongSelect:
                out[1] = data1 & 0x7F;
                return 2;
            case MidiType::TuneRequest:
            case MidiType::Clock:
            case MidiType::Start:
            case MidiType::Continue:
            case MidiType::Stop:
            case MidiType::ActiveSensing:
            case MidiType::SystemReset:
                return 1;
            default:
                out[1] = data1 & 0x7F;
                out[2] = data2 & 0x7F;
                return 3;
        }
    }
};

}  // namespace ot
