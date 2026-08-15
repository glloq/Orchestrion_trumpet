#include "diagnostics/MidiMonitor.h"

namespace ot {

void MidiMonitor::setTypeFilter(bool notes, bool controllers, bool other) {
    showNotes_ = notes;
    showControllers_ = controllers;
    showOther_ = other;
}

void MidiMonitor::clear() {
    head_ = 0;
    count_ = 0;
    overflow_ = 0;
}

void MidiMonitor::onMidi(const MidiMessage& msg) {
    ++sinceTick_;
    if (paused_) return;

    const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint8_t>(msg.source));
    if ((sourceMask_ & bit) == 0) return;

    const bool isNote = msg.type == MidiType::NoteOn || msg.type == MidiType::NoteOff;
    const bool isController = msg.type == MidiType::ControlChange ||
                              msg.type == MidiType::PitchBend ||
                              msg.type == MidiType::ChannelPressure ||
                              msg.type == MidiType::PolyPressure;
    if (isNote && !showNotes_) return;
    if (isController && !showControllers_) return;
    if (!isNote && !isController && !showOther_) return;

    MonitorEntry& e = entries_[head_];
    e.timestampMs = msg.timestampMs ? msg.timestampMs : OT_MILLIS();
    e.source = msg.source;
    e.type = msg.type;
    e.channel = msg.channel;
    e.data1 = msg.data1;
    e.data2 = msg.data2;

    head_ = static_cast<uint16_t>((head_ + 1) % kMonitorCapacity);
    if (count_ < kMonitorCapacity) {
        ++count_;
    } else {
        ++overflow_;
    }
}

const MonitorEntry& MidiMonitor::entry(uint16_t index) const {
    if (index >= count_) index = count_ ? static_cast<uint16_t>(count_ - 1) : 0;
    const uint16_t start =
        static_cast<uint16_t>((head_ + kMonitorCapacity - count_) % kMonitorCapacity);
    return entries_[(start + index) % kMonitorCapacity];
}

void MidiMonitor::tick(uint32_t nowMs) {
    if (lastTickMs_ == 0) {
        lastTickMs_ = nowMs;
        return;
    }
    const uint32_t elapsed = nowMs - lastTickMs_;
    if (elapsed < 1000) return;
    perSecond_ = static_cast<uint16_t>((sinceTick_ * 1000UL) / elapsed);
    sinceTick_ = 0;
    lastTickMs_ = nowMs;
}

const char* MidiMonitor::typeName(MidiType type) {
    switch (type) {
        case MidiType::NoteOff:
            return "Note Off";
        case MidiType::NoteOn:
            return "Note On";
        case MidiType::PolyPressure:
            return "Poly Pressure";
        case MidiType::ControlChange:
            return "Control Change";
        case MidiType::ProgramChange:
            return "Program Change";
        case MidiType::ChannelPressure:
            return "Channel Pressure";
        case MidiType::PitchBend:
            return "Pitch Bend";
        case MidiType::SystemExclusive:
            return "SysEx";
        case MidiType::Clock:
            return "Clock";
        case MidiType::Start:
            return "Start";
        case MidiType::Continue:
            return "Continue";
        case MidiType::Stop:
            return "Stop";
        case MidiType::ActiveSensing:
            return "Active Sensing";
        case MidiType::SystemReset:
            return "Reset";
        default:
            return "Other";
    }
}

}  // namespace ot
