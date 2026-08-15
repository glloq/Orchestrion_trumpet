// ============================================================================
//  Mocks.h - simulated drivers.
//
//  They let the whole logic be exercised on a PC: no ESP32, no servo, no
//  speaker.  Each mock records what it was asked to do so a test can assert on
//  the sequence of calls rather than on a side effect it cannot observe.
// ============================================================================
#pragma once

#include "audio/IAudioBackend.h"
#include "midi/IMidiTransport.h"
#include "valves/IValveActuator.h"

namespace ot {
namespace mock {

class MockAudioBackend final : public IAudioBackend {
public:
    void configure(const AudioConfig& cfg) override { cfg_ = cfg; }
    bool begin() override {
        if (failBegin) return false;
        running = true;
        return true;
    }
    void end() override { running = false; }
    void writeSamples(const int16_t* buffer, size_t count) override {
        ++writeCalls;
        samplesWritten += count;
        for (size_t i = 0; i < count; ++i) {
            const int32_t v = buffer[i] < 0 ? -buffer[i] : buffer[i];
            if (v > peak) peak = v;
        }
        if (muted) ++writesWhileMuted;
    }
    void mute(bool state) override {
        muted = state;
        ++muteCalls;
    }
    bool isMuted() const override { return muted; }
    const char* name() const override { return "MOCK"; }
    uint32_t sampleRate() const override { return cfg_.sampleRate; }
    uint8_t bitDepth() const override { return 16; }
    bool isRunning() const override { return running; }
    BackendMaturity maturity() const override { return BackendMaturity::STABLE; }

    AudioConfig cfg_;
    bool failBegin = false;
    bool running = false;
    bool muted = true;
    uint32_t writeCalls = 0;
    uint32_t writesWhileMuted = 0;
    size_t samplesWritten = 0;
    int32_t peak = 0;
    uint32_t muteCalls = 0;
};

class MockValveActuator final : public IValveActuator {
public:
    bool begin() override {
        ++beginCalls;
        return !failBegin;
    }
    void setValve(uint8_t valve, bool pressed) override {
        if (valve >= kMaxValves) return;
        if (stopped) {
            ++ignoredWhileStopped;
            return;
        }
        pressedState[valve] = pressed;
        ++setCalls;
    }
    void releaseAll() override {
        for (auto& p : pressedState) p = false;
        ++releaseAllCalls;
    }
    void update() override { ++updateCalls; }
    void emergencyStop() override {
        releaseAll();
        stopped = true;
        ++emergencyStopCalls;
    }
    void enable() override { stopped = false; }
    bool isPressed(uint8_t valve) const override {
        return valve < kMaxValves && pressedState[valve];
    }
    bool hasFault(uint8_t valve) const override { return fault[valve]; }
    const char* faultText(uint8_t valve) const override {
        return fault[valve] ? "mock fault" : nullptr;
    }

    bool pressedState[kMaxValves] = {false, false, false, false};
    bool fault[kMaxValves] = {false, false, false, false};
    bool failBegin = false;
    bool stopped = false;
    uint32_t beginCalls = 0;
    uint32_t setCalls = 0;
    uint32_t releaseAllCalls = 0;
    uint32_t updateCalls = 0;
    uint32_t emergencyStopCalls = 0;
    uint32_t ignoredWhileStopped = 0;
};

class MockMidiTransport final : public IMidiTransport {
public:
    explicit MockMidiTransport(MidiPort port) : port_(port) {}

    bool begin() override {
        started = true;
        return true;
    }
    void end() override { started = false; }
    void poll() override { ++pollCalls; }
    void onMidi(const MidiMessage& msg) override {
        if (sentCount < kCapacity) sent[sentCount] = msg;
        ++sentCount;
    }
    bool isConnected() const override { return connected; }
    MidiPort port() const override { return port_; }
    bool inputEnabled() const override { return inEnabled; }
    bool outputEnabled() const override { return outEnabled; }

    // Simulates something arriving on the wire.
    void receive(const MidiMessage& msg) {
        MidiMessage m = msg;
        m.source = port_;
        deliver(m);
    }

    static constexpr uint16_t kCapacity = 64;
    MidiPort port_;
    bool started = false;
    bool connected = true;
    bool inEnabled = true;
    bool outEnabled = true;
    uint32_t pollCalls = 0;
    uint16_t sentCount = 0;
    MidiMessage sent[kCapacity];
};

// Collects everything the router hands to an engine.
class RecordingSink final : public IMidiSink {
public:
    void onMidi(const MidiMessage& msg) override {
        if (count < kCapacity) messages[count] = msg;
        ++count;
    }
    void clear() { count = 0; }
    bool sawNoteOn(uint8_t note) const {
        for (uint16_t i = 0; i < count && i < kCapacity; ++i) {
            if (messages[i].isNoteOn() && messages[i].data1 == note) return true;
        }
        return false;
    }
    bool sawController(uint8_t number) const {
        for (uint16_t i = 0; i < count && i < kCapacity; ++i) {
            if (messages[i].type == MidiType::ControlChange && messages[i].data1 == number) {
                return true;
            }
        }
        return false;
    }

    static constexpr uint16_t kCapacity = 64;
    uint16_t count = 0;
    MidiMessage messages[kCapacity];
};

}  // namespace mock
}  // namespace ot
