// ============================================================================
//  MidiMonitor.h - the ring buffer behind the web MIDI monitor.
//
//  Bounded on purpose: a stuck sequencer can send thousands of messages per
//  second and the monitor must never be able to exhaust the heap.  Old entries
//  are overwritten, the UI is told how many were skipped.
// ============================================================================
#pragma once

#include "midi/IMidiTransport.h"

namespace ot {

static constexpr uint16_t kMonitorCapacity = 128;

struct MonitorEntry {
    uint32_t timestampMs = 0;
    MidiPort source = MidiPort::NONE;
    MidiType type = MidiType::Invalid;
    uint8_t channel = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

class MidiMonitor final : public IMidiSink {
public:
    void onMidi(const MidiMessage& msg) override;

    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    void clear();

    // Filters applied before an entry is stored.
    void setSourceFilter(uint16_t portMask) { sourceMask_ = portMask; }
    void setTypeFilter(bool notes, bool controllers, bool other);

    // Read back so the web UI can show the filter that is really in force
    // rather than a copy it keeps on its own side.
    uint16_t sourceFilter() const { return sourceMask_; }
    bool showsNotes() const { return showNotes_; }
    bool showsControllers() const { return showControllers_; }
    bool showsOther() const { return showOther_; }

    uint16_t count() const { return count_; }
    // 0 = oldest kept entry.
    const MonitorEntry& entry(uint16_t index) const;
    uint32_t overflowCount() const { return overflow_; }

    // Messages per second, refreshed by the diagnostics task.
    void tick(uint32_t nowMs);
    uint16_t messagesPerSecond() const { return perSecond_; }

    static const char* typeName(MidiType type);

private:
    MonitorEntry entries_[kMonitorCapacity];
    uint16_t head_ = 0;
    uint16_t count_ = 0;
    uint32_t overflow_ = 0;
    uint32_t sinceTick_ = 0;
    uint32_t lastTickMs_ = 0;
    uint16_t perSecond_ = 0;
    uint16_t sourceMask_ = 0xFFFF;
    bool showNotes_ = true;
    bool showControllers_ = true;
    bool showOther_ = true;
    bool paused_ = false;
};

}  // namespace ot
