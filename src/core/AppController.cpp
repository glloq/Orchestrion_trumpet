#include "core/AppController.h"

#include "audio/AudioBackendFactory.h"
#include "config/BoardCaps.h"
#include "core/EventBus.h"
#include "diagnostics/Logger.h"
#include "network/WebServer.h"
#include "network/WebSocketServer.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace ot {

namespace {
constexpr uint32_t kAudioStack = 6144;
constexpr uint32_t kMidiStack = 4096;
constexpr uint32_t kActuatorStack = 3072;
constexpr uint32_t kNetworkStack = 8192;
}  // namespace

AppController& AppController::instance() {
    static AppController app;
    return app;
}

bool AppController::begin() {
    SystemState& state = SystemState::instance();
    state.mutableStatus().bootTimeMs = OT_MILLIS();
    state.setMode(RunMode::BOOTING);

    initBoardCaps();
    const BoardCapabilities& caps = boardCaps();
    OT_LOGI("boot", "%s, %u core(s), flash %u KB, PSRAM %u KB", caps.chipName, caps.cores,
            static_cast<unsigned>(caps.flashSizeBytes / 1024),
            static_cast<unsigned>(caps.psramSizeBytes / 1024));

    // ---- 1. audio muted, valves released, solenoids disabled ---------------
    // Nothing has been initialised yet, so this is simply the guarantee that
    // no driver is created before the configuration has been validated.
    audioEngine_.setMuted(true);

    // ---- 2. load configuration -------------------------------------------
    if (!config_.begin()) {
        state.raiseFault(FaultCode::FILESYSTEM, "LittleFS could not be mounted");
    }
    const ConfigLoadResult loadResult = config_.load();
    switch (loadResult) {
        case ConfigLoadResult::DEFAULTS_NO_FILE:
            OT_LOGI("boot", "first start: using the STANDARD preset");
            break;
        case ConfigLoadResult::LOADED_AND_MIGRATED:
            OT_LOGI("boot", "configuration migrated to schema v%u",
                    static_cast<unsigned>(kConfigSchemaVersion));
            break;
        case ConfigLoadResult::DEFAULTS_CORRUPT:
            state.raiseFault(FaultCode::CONFIG_INVALID,
                             "the stored configuration is invalid: SAFE MODE");
            break;
        case ConfigLoadResult::DEFAULTS_TOO_NEW:
            state.raiseFault(FaultCode::CONFIG_INVALID,
                             "the configuration was written by a newer firmware: SAFE MODE");
            break;
        default:
            break;
    }

    const bool safe = config_.safeModeRequired();

    // ---- 3. router and MIDI transports ------------------------------------
    router_.configure(config_.config().midi);
    router_.setSink(MidiPort::MONITOR, &monitor_);
    router_.setSink(MidiPort::SOUND_ENGINE, &audioEngine_);
    router_.setSink(MidiPort::VALVE_ENGINE, &valves_);

    // ---- 4. peripherals ---------------------------------------------------
    if (!safe) {
        if (!startValves()) {
            state.raiseFault(FaultCode::VALVE_DRIVER_FAILED,
                             "at least one valve driver failed to start");
        }
        if (!startAudio()) {
            state.raiseFault(FaultCode::AUDIO_BACKEND_FAILED,
                             "the configured audio backend could not be started");
        }
        startMidi();
    } else {
        OT_LOGW("boot", "SAFE MODE: no actuator and no audio backend will be started");
    }

    // ---- 5. network and web UI (always, even in safe mode) ----------------
    startNetwork();
    startTasks();

    // ---- 6. unmute -------------------------------------------------------
    if (!safe) {
        state.setMode(RunMode::RUNNING);
        if (backend_ && backend_->isRunning()) {
            backend_->mute(false);
            audioEngine_.setMuted(false);
            SystemState::instance().mutableStatus().audioMuted = false;
        }
    } else {
        state.setMode(RunMode::SAFE_MODE);
    }

    EventBus::instance().publish(EventType::ModeChanged, static_cast<uint32_t>(state.mode()));
    OT_LOGI("boot", "ready in %s", SystemState::toString(state.mode()));
    return !safe;
}

bool AppController::safeMode() const {
    return SystemState::instance().mode() == RunMode::SAFE_MODE;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
bool AppController::startAudio() {
    const InstrumentConfiguration& cfg = config_.config();

    audioEngine_.configure(cfg.audio, cfg.speaker, cfg.amplifier, cfg.acoustic, cfg.instrument,
                           cfg.valves);
    audioEngine_.begin();
    audioEngine_.setMuted(true);

    backend_ = createAudioBackend(cfg.audio.backend);
    if (!backend_) {
        OT_LOGE("audio", "backend %s is not available on this board",
                toString(cfg.audio.backend));
        backend_ = createAudioBackend(AudioBackendType::NONE);
    }
    backend_->configure(cfg.audio);

    blockFrames_ = cfg.audio.blockSize ? cfg.audio.blockSize : 128;
    renderBuffer_ = static_cast<float*>(malloc(blockFrames_ * sizeof(float)));
    outputBuffer_ = static_cast<int32_t*>(malloc(blockFrames_ * sizeof(int32_t)));
    if (!renderBuffer_ || !outputBuffer_) {
        OT_LOGE("audio", "could not allocate the %u frame audio buffers",
                static_cast<unsigned>(blockFrames_));
        return false;
    }

    if (!backend_->begin()) {
        const char* err = backend_->lastError();
        OT_LOGE("audio", "%s failed to start%s%s", backend_->name(), err ? ": " : "",
                err ? err : "");
        destroyAudioBackend(backend_);
        backend_ = createAudioBackend(AudioBackendType::NONE);
        backend_->configure(cfg.audio);
        backend_->begin();
        SystemState::instance().mutableStatus().audioRunning = false;
        return false;
    }

    SystemState::instance().mutableStatus().audioRunning = true;
    OT_LOGI("audio", "%s running at %u Hz / %u bit (%s)", backend_->name(),
            static_cast<unsigned>(backend_->sampleRate()),
            static_cast<unsigned>(backend_->bitDepth()), toString(backend_->maturity()));
    return true;
}

void AppController::stopAudio() {
    audioRunning_ = false;
#if !defined(OT_HOST_BUILD)
    vTaskDelay(pdMS_TO_TICKS(20));
#endif
    if (backend_) {
        backend_->mute(true);
        destroyAudioBackend(backend_);
        backend_ = nullptr;
    }
    SystemState::instance().mutableStatus().audioRunning = false;
}

// ---------------------------------------------------------------------------
// Valves
// ---------------------------------------------------------------------------
bool AppController::startValves() {
    const InstrumentConfiguration& cfg = config_.config();
    const bool ok = valves_.begin(cfg.valves, cfg.instrument);
    // Whatever happened, the valves are parked: begin() releases them all.
    valves_.releaseAll();
    return ok;
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------
void AppController::startMidi() {
    const MidiConfig& midi = config_.config().midi;

    usb_.configure(midi.usb);
    ble_.configure(midi.ble);
    rtp_.configure(midi.rtp);
    din_.configure(midi.din);
    web_.configure(midi.web);

    router_.setTransport(MidiPort::USB, &usb_);
    router_.setTransport(MidiPort::BLE, &ble_);
    router_.setTransport(MidiPort::RTP, &rtp_);
    router_.setTransport(MidiPort::DIN, &din_);
    router_.setTransport(MidiPort::WEB, &web_);

    // Each transport reports whether it actually started; the web UI shows the
    // real state instead of the requested one.
    if (midi.usb.inEnabled || midi.usb.outEnabled) usb_.begin();
    if (midi.ble.inEnabled || midi.ble.outEnabled) ble_.begin();
    if (midi.din.inEnabled || midi.din.outEnabled || midi.din.thruEnabled) din_.begin();
    web_.begin();
    // RTP needs an IP address, so it is started later by the network task.
}

void AppController::stopMidi() {
    usb_.end();
    ble_.end();
    rtp_.end();
    din_.end();
    web_.end();
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
void AppController::startNetwork() {
    const InstrumentConfiguration& cfg = config_.config();
    wifi_.configure(cfg.wifi);
    wifi_.begin();
    if (cfg.wifi.captivePortal && wifi_.apActive()) portal_.begin();

    webSocketServer().begin(this);
    webServer().begin(this);
}

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------
void AppController::startTasks() {
    if (tasksStarted_) return;
    tasksStarted_ = true;
    audioRunning_ = true;

#if !defined(OT_HOST_BUILD)
    // Audio and MIDI live on the application core; the network stack and the
    // web server stay on core 0 with the Wi-Fi driver, so an HTTP request can
    // never delay a note.
    xTaskCreatePinnedToCore(audioTaskEntry, "ot_audio", kAudioStack, this, 20, nullptr, 1);
    xTaskCreatePinnedToCore(midiTaskEntry, "ot_midi", kMidiStack, this, 18, nullptr, 1);
    xTaskCreatePinnedToCore(actuatorTaskEntry, "ot_valves", kActuatorStack, this, 10, nullptr, 1);
    xTaskCreatePinnedToCore(networkTaskEntry, "ot_net", kNetworkStack, this, 5, nullptr, 0);
#endif
}

void AppController::audioTaskEntry(void* arg) { static_cast<AppController*>(arg)->audioTask(); }
void AppController::midiTaskEntry(void* arg) { static_cast<AppController*>(arg)->midiTask(); }
void AppController::actuatorTaskEntry(void* arg) {
    static_cast<AppController*>(arg)->actuatorTask();
}
void AppController::networkTaskEntry(void* arg) {
    static_cast<AppController*>(arg)->networkTask();
}

void AppController::audioTask() {
#if !defined(OT_HOST_BUILD)
    const uint32_t blockUs =
        backend_ && backend_->sampleRate()
            ? static_cast<uint32_t>((blockFrames_ * 1000000ULL) / backend_->sampleRate())
            : 2666;

    while (audioRunning_) {
        if (!backend_ || !renderBuffer_ || !outputBuffer_) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        const uint32_t start = micros();
        audioEngine_.renderBlock(renderBuffer_, blockFrames_);

        // Float -> Q31.  The backend narrows to 16 bit by itself when needed.
        for (size_t i = 0; i < blockFrames_; ++i) {
            float s = renderBuffer_[i];
            if (s > 0.999999f) s = 0.999999f;
            if (s < -0.999999f) s = -0.999999f;
            outputBuffer_[i] = static_cast<int32_t>(s * 2147483392.0f);
        }
        backend_->writeSamples32(outputBuffer_, blockFrames_);

        const uint32_t elapsed = micros() - start;
        ++audioBlocks_;
        // Exponential average of the fraction of the block period we spent
        // working; the DMA wait is included, so it never exceeds 100%.
        const float load = blockUs ? (static_cast<float>(elapsed) * 100.0f /
                                      static_cast<float>(blockUs))
                                   : 0.0f;
        audioCpu_ += (load - audioCpu_) * 0.02f;
    }
    vTaskDelete(nullptr);
#endif
}

void AppController::midiTask() {
#if !defined(OT_HOST_BUILD)
    while (true) {
        usb_.poll();
        ble_.poll();
        din_.poll();
        rtp_.poll();
        // 1 ms cadence: well under the 320 us of a DIN byte burst thanks to
        // the UART FIFO, and far below anything a player could feel.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#endif
}

void AppController::actuatorTask() {
#if !defined(OT_HOST_BUILD)
    while (true) {
        valves_.update();
        // 5 ms is fast enough for a servo ramp and for the solenoid guard,
        // and leaves the core almost entirely to audio and MIDI.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
#endif
}

void AppController::forceHotspot() {
    wifi_.forceAccessPoint();
    // The portal follows the hotspot: a phone that joins lands on the config
    // page by itself, which is the whole point of the escape hatch.
    if (config_.config().wifi.captivePortal) portal_.begin();
}

void AppController::pollBootButton(uint32_t nowMs) {
#if !defined(OT_HOST_BUILD)
    // GPIO0 is the BOOT button on both reference dev boards.  It is a strapping
    // pin, sampled only at reset, so reading it afterwards is free and safe.
    constexpr uint8_t kBootButtonPin = 0;
    constexpr uint32_t kHoldMs = 2000;
    static bool configured = false;
    if (!configured) {
        pinMode(kBootButtonPin, INPUT_PULLUP);
        configured = true;
    }

    const bool pressed = digitalRead(kBootButtonPin) == LOW;
    if (!pressed) {
        bootButtonHeldSinceMs_ = 0;
        bootButtonLatched_ = false;
        return;
    }
    if (bootButtonHeldSinceMs_ == 0) {
        bootButtonHeldSinceMs_ = nowMs;
        return;
    }
    if (!bootButtonLatched_ && (nowMs - bootButtonHeldSinceMs_) >= kHoldMs) {
        bootButtonLatched_ = true;   // one action per press, not one per poll
        OT_LOGW("wifi", "BOOT button held: raising the hotspot");
        forceHotspot();
    }
#else
    (void)nowMs;
#endif
}

void AppController::networkTask() {
#if !defined(OT_HOST_BUILD)
    bool rtpStarted = false;
    while (true) {
        const uint32_t now = millis();
        pollBootButton(now);
        wifi_.loop();
        portal_.loop();
        webServer().loop();
        webSocketServer().loop();

        // RTP-MIDI can only bind once an interface has an address.
        const MidiRtpConfig& rtpCfg = config_.config().midi.rtp;
        if (!rtpStarted && (rtpCfg.inEnabled || rtpCfg.outEnabled) &&
            (wifi_.staConnected() || wifi_.apActive())) {
            rtpStarted = rtp_.begin();
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
#endif
}

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------
void AppController::tick() {
    const uint32_t now = OT_MILLIS();

    if (rebootAtMs_ != 0 && static_cast<int32_t>(now - rebootAtMs_) >= 0) {
#if !defined(OT_HOST_BUILD)
        OT_LOGW("system", "rebooting");
        ESP.restart();
#endif
    }

    if (now - lastTelemetryMs_ < 500) return;
    const uint32_t elapsed = now - lastTelemetryMs_;
    lastTelemetryMs_ = now;

    SystemStatus& s = SystemState::instance().mutableStatus();
    const MidiRouterStats& stats = router_.stats();
    s.midiRxPerSecond =
        static_cast<uint16_t>(((stats.rxTotal - lastMidiRx_) * 1000UL) / (elapsed ? elapsed : 1));
    s.midiTxPerSecond =
        static_cast<uint16_t>(((stats.txTotal - lastMidiTx_) * 1000UL) / (elapsed ? elapsed : 1));
    lastMidiRx_ = stats.rxTotal;
    lastMidiTx_ = stats.txTotal;
    s.midiRxTotal = stats.rxTotal;
    s.midiTxTotal = stats.txTotal;

    s.audioCpuPercent = audioCpu_;
    s.audioBlocks = audioBlocks_;
    s.audioUnderruns = backend_ ? backend_->underruns() : 0;
    s.audioMuted = backend_ ? backend_->isMuted() : true;
    s.audioRunning = backend_ && backend_->isRunning();
    s.apActive = wifi_.apActive();
    s.staConnected = wifi_.staConnected();
    s.wifiRssi = wifi_.rssi();

    monitor_.tick(now);

    // A solenoid that tripped its thermal guard must be visible even if the
    // user never opens the diagnostics page.
    if (valves_.anyFault() && !SystemState::instance().hasFault(FaultCode::SOLENOID_THERMAL)) {
        SystemState::instance().raiseFault(FaultCode::SOLENOID_THERMAL,
                                           "a valve driver reported a fault");
    }
}

// ---------------------------------------------------------------------------
// Global actions
// ---------------------------------------------------------------------------
void AppController::panic() {
    OT_LOGW("panic", "PANIC requested");
    // Order matters: silence first, then park the mechanics.
    audioEngine_.panic();
    if (backend_) backend_->mute(true);
    audioEngine_.setMuted(true);
    valves_.panic();
    router_.broadcastPanic();
    SystemState::instance().setMode(RunMode::PANIC);
    EventBus::instance().publish(EventType::PanicRequested);
}

void AppController::releasePanic() {
    if (SystemState::instance().mode() != RunMode::PANIC) return;
    valves_.enableAfterPanic();
    SystemState::instance().clearFault(FaultCode::SOLENOID_THERMAL);
    if (backend_ && backend_->isRunning()) {
        backend_->mute(false);
        audioEngine_.setMuted(false);
    }
    SystemState::instance().setMode(RunMode::RUNNING);
}

void AppController::reboot(uint32_t delayMs) {
    // Leave the instrument safe before the reset: a watchdog reboot must never
    // find a solenoid energised.
    panic();
    rebootAtMs_ = OT_MILLIS() + (delayMs ? delayMs : 250);
}

bool AppController::applyConfiguration(const InstrumentConfiguration& candidate,
                                       ValidationReport& report) {
    if (!config_.applyAndSave(candidate, report)) return false;
    EventBus::instance().publish(EventType::ConfigSaved);
    return true;
}

bool AppController::factoryReset() {
    const bool ok = config_.factoryReset();
    reboot(500);
    return ok;
}

}  // namespace ot
