// ============================================================================
//  AppController.h - owns every module and the FreeRTOS tasks.
//
//  Boot sequence (docs/ARCHITECTURE.md):
//      mute audio -> release valves -> disable solenoids -> load configuration
//      -> validate hardware -> initialise peripherals -> start MIDI
//      -> start network -> unmute audio
//  An invalid configuration stops the sequence at "validate" and the
//  instrument comes up in SAFE MODE: Wi-Fi AP and the web configuration only,
//  no actuator ever energised.
//
//  Tasks and priorities:
//      audio      core 1, prio 20   blocks on the I2S DMA
//      midi       core 1, prio 18   polls every transport
//      actuators  core 1, prio 10   servo ramps, solenoid guard
//      network    core 0, prio  5   HTTP, WebSocket, Wi-Fi, captive portal
//      loop()     core 1, prio  1   telemetry and logging only
// ============================================================================
#pragma once

#include "audio/AudioEngine.h"
#include "audio/IAudioBackend.h"
#include "config/ConfigManager.h"
#include "core/SystemState.h"
#include "diagnostics/MidiMonitor.h"
#include "midi/MidiBle.h"
#include "midi/MidiDin.h"
#include "midi/MidiRouter.h"
#include "midi/MidiRtp.h"
#include "midi/MidiUsb.h"
#include "midi/MidiWebSocket.h"
#include "network/CaptivePortal.h"
#include "network/WifiManager.h"
#include "valves/ValveController.h"

namespace ot {

class AppController {
public:
    static AppController& instance();

    // Full boot sequence.  Returns false when the instrument had to fall back
    // to SAFE MODE (the web UI still comes up).
    bool begin();
    // Called from loop(): telemetry only, never anything time critical.
    void tick();

    // ---- global actions -------------------------------------------------
    void panic();
    void releasePanic();
    void reboot(uint32_t delayMs);
    bool applyConfiguration(const InstrumentConfiguration& candidate, ValidationReport& report);
    bool factoryReset();
    // Brings the hotspot up on demand (web UI, or the board's BOOT button) and
    // starts the captive portal with it.
    void forceHotspot();

    // ---- accessors used by the web layer --------------------------------
    ConfigManager& configManager() { return config_; }
    const InstrumentConfiguration& config() const { return config_.config(); }
    MidiRouter& router() { return router_; }
    AudioEngine& audio() { return audioEngine_; }
    IAudioBackend* backend() { return backend_; }
    ValveController& valves() { return valves_; }
    MidiMonitor& monitor() { return monitor_; }
    WifiManager& wifi() { return wifi_; }
    MidiWebSocketTransport& webMidi() { return web_; }
    MidiBleTransport& bleMidi() { return ble_; }
    MidiUsbTransport& usbMidi() { return usb_; }
    MidiDinTransport& dinMidi() { return din_; }
    MidiRtpTransport& rtpMidi() { return rtp_; }

    bool safeMode() const;
    float audioCpuPercent() const { return audioCpu_; }
    uint32_t audioBlocks() const { return audioBlocks_; }

private:
    AppController() = default;

    bool startAudio();
    void stopAudio();
    bool startValves();
    void startMidi();
    void stopMidi();
    void startNetwork();
    void startTasks();

    static void audioTaskEntry(void* arg);
    static void midiTaskEntry(void* arg);
    static void actuatorTaskEntry(void* arg);
    static void networkTaskEntry(void* arg);

    void audioTask();
    void midiTask();
    void actuatorTask();
    void networkTask();
    // Escape hatch: holding the board's BOOT button raises the hotspot even
    // when the stored station credentials are wrong and the user cannot reach
    // the web UI any other way.
    void pollBootButton(uint32_t nowMs);

    ConfigManager config_;
    MidiRouter router_;
    MidiUsbTransport usb_;
    MidiBleTransport ble_;
    MidiRtpTransport rtp_;
    MidiDinTransport din_;
    MidiWebSocketTransport web_;
    MidiMonitor monitor_;
    AudioEngine audioEngine_;
    IAudioBackend* backend_ = nullptr;
    ValveController valves_;
    WifiManager wifi_;
    CaptivePortal portal_;

    // Audio scratch buffers, allocated once at boot and never touched again.
    float* renderBuffer_ = nullptr;
    int32_t* outputBuffer_ = nullptr;
    size_t blockFrames_ = 128;

    float audioCpu_ = 0.0f;
    uint32_t audioBlocks_ = 0;
    uint32_t lastTelemetryMs_ = 0;
    uint32_t rebootAtMs_ = 0;
    uint32_t lastMidiRx_ = 0;
    uint32_t lastMidiTx_ = 0;
    uint32_t bootButtonHeldSinceMs_ = 0;
    bool bootButtonLatched_ = false;
    bool tasksStarted_ = false;
    volatile bool audioRunning_ = false;
};

}  // namespace ot
