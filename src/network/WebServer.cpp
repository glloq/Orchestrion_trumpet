#include "network/WebServer.h"

#include <ArduinoJson.h>

#include "audio/AudioBackendFactory.h"
#include "config/BoardCaps.h"
#include "config/ConfigSerializer.h"
#include "config/Presets.h"
#include "core/AppController.h"
#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#endif

namespace ot {

namespace {
constexpr uint16_t kPort = 80;
constexpr size_t kJsonBuffer = 8192;

#if !defined(OT_HOST_BUILD)
::WebServer* g_http = nullptr;
// One shared serialisation buffer: the HTTP handlers all run in the same task,
// one at a time, and this keeps 8 kB off the stack.
char g_buffer[kJsonBuffer];

// True only when the partition table actually has a second application slot.
// On a 4 MB ESP32-WROOM there is none (see partitions/), and the Firmware page
// says so rather than offering an update that would fail halfway through.
bool otaAvailable() { return esp_ota_get_next_update_partition(nullptr) != nullptr; }

const char* contentTypeFor(const String& path) {
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".woff2")) return "font/woff2";
    return "text/plain";
}
#endif
}  // namespace

HttpServerModule& webServer() {
    static HttpServerModule module;
    return module;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
void HttpServerModule::sendJson(int code, const char* json, size_t length) {
#if !defined(OT_HOST_BUILD)
    if (!g_http) return;
    g_http->setContentLength(length);
    g_http->send(code, "application/json", "");
    g_http->sendContent(json, length);
#else
    (void)code;
    (void)json;
    (void)length;
#endif
}

void HttpServerModule::sendError(int code, const char* message) {
#if !defined(OT_HOST_BUILD)
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(code, g_buffer, n);
#else
    (void)code;
    (void)message;
#endif
}

void HttpServerModule::sendOk() {
#if !defined(OT_HOST_BUILD)
    static const char kOk[] = "{\"ok\":true}";
    sendJson(200, kOk, sizeof(kOk) - 1);
#endif
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool HttpServerModule::begin(AppController* app) {
    app_ = app;
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (started_) return true;
    g_http = new ::WebServer(kPort);
    registerRoutes();
    g_http->begin();
    started_ = true;
    OT_LOGI("http", "web UI on port %u", kPort);
    return true;
#endif
}

void HttpServerModule::end() {
#if !defined(OT_HOST_BUILD)
    if (g_http) {
        g_http->stop();
        delete g_http;
        g_http = nullptr;
    }
#endif
    started_ = false;
}

void HttpServerModule::loop() {
#if !defined(OT_HOST_BUILD)
    if (g_http) g_http->handleClient();
#endif
}

#if !defined(OT_HOST_BUILD)

void HttpServerModule::registerRoutes() {
    auto bind = [this](const char* uri, HTTPMethod method, void (HttpServerModule::*fn)()) {
        g_http->on(uri, method, [this, fn]() { (this->*fn)(); });
    };

    bind("/api/status", HTTP_GET, &HttpServerModule::handleStatus);
    bind("/api/config", HTTP_GET, &HttpServerModule::handleGetConfig);
    bind("/api/config", HTTP_PUT, &HttpServerModule::handlePutConfig);
    bind("/api/config", HTTP_POST, &HttpServerModule::handlePutConfig);
    bind("/api/config/validate", HTTP_POST, &HttpServerModule::handleValidate);
    bind("/api/config/export", HTTP_GET, &HttpServerModule::handleExport);
    bind("/api/config/import", HTTP_POST, &HttpServerModule::handleImport);
    bind("/api/config/preset", HTTP_POST, &HttpServerModule::handlePreset);
    bind("/api/hardware", HTTP_GET, &HttpServerModule::handleHardware);
    bind("/api/panic", HTTP_POST, &HttpServerModule::handlePanic);
    bind("/api/panic/release", HTTP_POST, &HttpServerModule::handleReleasePanic);
    bind("/api/audio/test", HTTP_POST, &HttpServerModule::handleAudioTest);
    bind("/api/audio/mute", HTTP_POST, &HttpServerModule::handleAudioMute);
    bind("/api/audio/volume", HTTP_POST, &HttpServerModule::handleAudioVolume);
    bind("/api/valve/test", HTTP_POST, &HttpServerModule::handleValveTest);
    bind("/api/valve/mode", HTTP_POST, &HttpServerModule::handleValveMode);
    bind("/api/valve/manual", HTTP_POST, &HttpServerModule::handleValveManual);
    bind("/api/valve/calibrate", HTTP_POST, &HttpServerModule::handleValveCalibrate);
    bind("/api/midi/status", HTTP_GET, &HttpServerModule::handleMidiStatus);
    bind("/api/midi/monitor", HTTP_GET, &HttpServerModule::handleMidiMonitor);
    bind("/api/fingering", HTTP_GET, &HttpServerModule::handleGetFingering);
    bind("/api/fingering", HTTP_PUT, &HttpServerModule::handlePutFingering);
    bind("/api/fingering/reset", HTTP_POST, &HttpServerModule::handleResetFingering);
    bind("/api/diagnostics", HTTP_GET, &HttpServerModule::handleDiagnostics);
    bind("/api/system/reboot", HTTP_POST, &HttpServerModule::handleReboot);
    bind("/api/system/factory-reset", HTTP_POST, &HttpServerModule::handleFactoryReset);

    // OTA: the browser POSTs the .bin, the upload handler streams it into the
    // inactive OTA slot.  Actuators and audio are parked before the first byte.
    g_http->on(
        "/api/ota", HTTP_POST, [this]() { handleOtaFinished(); },
        [this]() { handleOtaUpload(); });

    g_http->onNotFound([this]() { handleNotFound(); });
}

bool HttpServerModule::serveStatic(const char* uri) {
    String path(uri);
    if (path.endsWith("/")) path += "index.html";
    if (!path.startsWith("/")) path = "/" + path;

    // Pre-compressed assets are preferred: the web UI ships gzipped so it fits
    // comfortably in LittleFS and loads fast over the hotspot.
    const String gz = path + ".gz";
    if (LittleFS.exists(gz)) {
        File f = LittleFS.open(gz, "r");
        if (f) {
            g_http->sendHeader("Content-Encoding", "gzip");
            g_http->sendHeader("Cache-Control", "max-age=86400");
            g_http->streamFile(f, contentTypeFor(path));
            f.close();
            return true;
        }
    }
    if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        if (f) {
            g_http->sendHeader("Cache-Control", "max-age=86400");
            g_http->streamFile(f, contentTypeFor(path));
            f.close();
            return true;
        }
    }
    return false;
}

void HttpServerModule::handleNotFound() {
    if (serveStatic(g_http->uri().c_str())) return;

    // Captive portal: any unknown host lands on the configuration page.
    if (!g_http->uri().startsWith("/api/")) {
        if (serveStatic("/index.html")) return;
        g_http->send(200, "text/html",
                     "<!doctype html><meta charset=utf-8><title>MIDI Trumpet</title>"
                     "<h1>Orchestrion Trumpet</h1>"
                     "<p>The web interface is not installed on this board.</p>"
                     "<p>Upload it with <code>pio run -t uploadfs</code>.</p>");
        return;
    }
    sendError(404, "unknown endpoint");
}

// ---------------------------------------------------------------------------
// Status / diagnostics
// ---------------------------------------------------------------------------
void HttpServerModule::handleStatus() {
    const SystemStatus& s = SystemState::instance().status();
    const AudioEngineStatus a = app_->audio().status();
    const InstrumentConfiguration& cfg = app_->config();

    JsonDocument doc;
    doc["ok"] = true;
    doc["firmware"] = OT_FIRMWARE_VERSION;
    doc["mode"] = SystemState::toString(s.mode);
    doc["uptime"] = SystemState::instance().uptimeSeconds();
    doc["deviceName"] = cfg.system.deviceName;
    doc["wizardCompleted"] = cfg.system.wizardCompleted;
    doc["preset"] = cfg.system.presetName;

    JsonObject audio = doc["audio"].to<JsonObject>();
    audio["backend"] = app_->backend() ? app_->backend()->name() : "NONE";
    audio["running"] = s.audioRunning;
    audio["muted"] = s.audioMuted;
    audio["sampleRate"] = app_->backend() ? app_->backend()->sampleRate() : 0;
    audio["bitDepth"] = app_->backend() ? app_->backend()->bitDepth() : 0;
    audio["underruns"] = s.audioUnderruns;
    audio["cpu"] = s.audioCpuPercent;
    audio["volume"] = app_->audio().masterVolume();
    audio["peakScale"] = app_->audio().safePeakScale();
    audio["note"] = a.noteActive ? a.note : 0;
    audio["velocity"] = a.velocity;
    audio["frequency"] = a.frequencyHz;

    JsonObject midi = doc["midi"].to<JsonObject>();
    midi["rxPerSecond"] = s.midiRxPerSecond;
    midi["txPerSecond"] = s.midiTxPerSecond;
    midi["usb"] = app_->usbMidi().isConnected();
    midi["ble"] = app_->bleMidi().isConnected();
    midi["din"] = app_->dinMidi().isConnected();
    midi["rtp"] = app_->rtpMidi().isConnected();

    JsonArray valves = doc["valves"].to<JsonArray>();
    for (uint8_t i = 0; i < app_->valves().valveCount(); ++i) {
        const ValveStatus v = app_->valves().status(i);
        JsonObject o = valves.add<JsonObject>();
        o["pressed"] = v.pressed;
        o["type"] = toString(v.type);
        o["fault"] = v.fault;
    }
    doc["valveMode"] = toString(app_->valves().mode());

    JsonObject net = doc["network"].to<JsonObject>();
    net["ap"] = s.apActive;
    net["sta"] = s.staConnected;
    net["ssid"] = app_->wifi().apSsid();
    net["ip"] = app_->wifi().ipAddress();
    net["hostname"] = app_->wifi().hostname();
    net["rssi"] = s.wifiRssi;
    net["clients"] = s.webClients;

    doc["otaAvailable"] = otaAvailable();
    doc["faults"] = s.faultMask;
    if (s.faultText[0]) doc["faultText"] = s.faultText;

    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handleDiagnostics() {
    const SystemStatus& s = SystemState::instance().status();
    JsonDocument doc;
    doc["ok"] = true;
    doc["firmware"] = OT_FIRMWARE_VERSION;
    doc["board"] = boardCaps().chipName;
    doc["cores"] = boardCaps().cores;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minFreeHeap"] = ESP.getMinFreeHeap();
    doc["psram"] = boardCaps().psramSizeBytes;
    doc["freePsram"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    doc["flash"] = boardCaps().flashSizeBytes;
    doc["sketchSize"] = ESP.getSketchSize();
    doc["freeSketchSpace"] = ESP.getFreeSketchSpace();
    doc["otaAvailable"] = otaAvailable();
    if (!otaAvailable()) {
        doc["otaReason"] =
            "this partition table has a single application slot: flash over USB, or use an "
            "8 MB module";
    }
    doc["uptime"] = SystemState::instance().uptimeSeconds();
    doc["resetReason"] = static_cast<int>(esp_reset_reason());
    doc["cpuLoad"] = s.audioCpuPercent;
    doc["audioUnderruns"] = s.audioUnderruns;
    doc["audioBlocks"] = app_->audioBlocks();
    doc["audioSampleRate"] = app_->backend() ? app_->backend()->sampleRate() : 0;
    doc["audioBackend"] = app_->backend() ? app_->backend()->name() : "NONE";
    doc["audioMaturity"] =
        app_->backend() ? toString(app_->backend()->maturity()) : "STABLE";
    doc["wifiRssi"] = s.wifiRssi;
    doc["ble"] = app_->bleMidi().isConnected();
    doc["midiRx"] = s.midiRxTotal;
    doc["midiTx"] = s.midiTxTotal;
    doc["midiRxPerSecond"] = s.midiRxPerSecond;
    doc["midiTxPerSecond"] = s.midiTxPerSecond;
    doc["monitorOverflow"] = app_->monitor().overflowCount();

    const MidiRouterStats& stats = app_->router().stats();
    JsonObject router = doc["router"].to<JsonObject>();
    router["droppedByFilter"] = stats.droppedByFilter;
    router["loopsSuppressed"] = stats.loopsSuppressed;

    JsonArray valves = doc["valves"].to<JsonArray>();
    for (uint8_t i = 0; i < app_->valves().valveCount(); ++i) {
        const ValveStatus v = app_->valves().status(i);
        JsonObject o = valves.add<JsonObject>();
        o["type"] = toString(v.type);
        o["pressed"] = v.pressed;
        o["angle"] = v.angle;
        o["duty"] = v.dutyPercent;
        o["fault"] = v.fault;
        if (v.faultText) o["faultText"] = v.faultText;
    }

    doc["faults"] = s.faultMask;
    if (s.faultText[0]) doc["faultText"] = s.faultText;

    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void HttpServerModule::handleGetConfig() {
    const size_t n = app_->configManager().exportJson(g_buffer, sizeof(g_buffer));
    if (n == 0) {
        sendError(500, "could not serialise the configuration");
        return;
    }
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handleExport() {
    const size_t n = app_->configManager().exportJson(g_buffer, sizeof(g_buffer));
    g_http->sendHeader("Content-Disposition", "attachment; filename=\"trumpet-config.json\"");
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handlePutConfig() {
    const String& body = g_http->arg("plain");
    if (body.isEmpty()) {
        sendError(400, "empty body");
        return;
    }

    InstrumentConfiguration candidate;
    if (!deserializeConfig(body.c_str(), body.length(), candidate)) {
        sendError(400, "the document is not a valid configuration");
        return;
    }

    ValidationReport report;
    const bool ok = app_->applyConfiguration(candidate, report);

    JsonDocument doc;
    doc["ok"] = ok;
    doc["rebootRequired"] = ok;
    JsonArray issues = doc["issues"].to<JsonArray>();
    for (uint8_t i = 0; i < report.count(); ++i) {
        JsonObject o = issues.add<JsonObject>();
        o["severity"] = ConfigValidator::toString(report.issue(i).severity);
        o["field"] = report.issue(i).field;
        o["message"] = report.issue(i).message;
    }
    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(ok ? 200 : 422, g_buffer, n);
}

void HttpServerModule::handleValidate() {
    const String& body = g_http->arg("plain");
    InstrumentConfiguration candidate;
    if (body.isEmpty() || !deserializeConfig(body.c_str(), body.length(), candidate)) {
        sendError(400, "the document is not a valid configuration");
        return;
    }

    ValidationReport report;
    ConfigValidator::validate(candidate, report);

    JsonDocument doc;
    doc["ok"] = !report.hasErrors();
    doc["truncated"] = report.truncated();
    JsonArray issues = doc["issues"].to<JsonArray>();
    for (uint8_t i = 0; i < report.count(); ++i) {
        JsonObject o = issues.add<JsonObject>();
        o["severity"] = ConfigValidator::toString(report.issue(i).severity);
        o["field"] = report.issue(i).field;
        o["message"] = report.issue(i).message;
    }
    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handleImport() {
    const String& body = g_http->arg("plain");
    if (body.isEmpty()) {
        sendError(400, "empty body");
        return;
    }
    ValidationReport report;
    const bool ok = app_->configManager().importJson(body.c_str(), body.length(), report);

    JsonDocument doc;
    doc["ok"] = ok;
    doc["rebootRequired"] = ok;
    JsonArray issues = doc["issues"].to<JsonArray>();
    for (uint8_t i = 0; i < report.count(); ++i) {
        JsonObject o = issues.add<JsonObject>();
        o["severity"] = ConfigValidator::toString(report.issue(i).severity);
        o["field"] = report.issue(i).field;
        o["message"] = report.issue(i).message;
    }
    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(ok ? 200 : 422, g_buffer, n);
}

void HttpServerModule::handlePreset() {
    const String& body = g_http->arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        sendError(400, "expected {\"preset\":\"STANDARD\"}");
        return;
    }
    const char* key = doc["preset"];
    const PresetInfo* info = findPreset(key ? key : "");
    if (!info) {
        sendError(400, "unknown preset");
        return;
    }

    // A preset only fills the audio chain; the rest of the configuration (MIDI,
    // valves, Wi-Fi, calibration) is preserved on purpose.
    InstrumentConfiguration candidate = app_->config();
    applyPreset(info->id, candidate);

    ValidationReport report;
    const bool ok = app_->applyConfiguration(candidate, report);

    JsonDocument out;
    out["ok"] = ok;
    out["rebootRequired"] = ok;
    JsonArray issues = out["issues"].to<JsonArray>();
    for (uint8_t i = 0; i < report.count(); ++i) {
        JsonObject o = issues.add<JsonObject>();
        o["severity"] = ConfigValidator::toString(report.issue(i).severity);
        o["field"] = report.issue(i).field;
        o["message"] = report.issue(i).message;
    }
    const size_t n = serializeJson(out, g_buffer, sizeof(g_buffer));
    sendJson(ok ? 200 : 422, g_buffer, n);
}

// ---------------------------------------------------------------------------
// Hardware catalogue: what this board can actually do
// ---------------------------------------------------------------------------
void HttpServerModule::handleHardware() {
    const BoardCapabilities& caps = boardCaps();
    JsonDocument doc;
    doc["ok"] = true;

    JsonObject board = doc["board"].to<JsonObject>();
    board["type"] = toString(caps.board);
    board["chip"] = caps.chipName;
    board["cores"] = caps.cores;
    board["gpioMax"] = caps.gpioMax;
    board["flash"] = caps.flashSizeBytes;
    board["psram"] = caps.psramSizeBytes;
    board["hasNativeUsb"] = caps.hasNativeUsb;
    board["hasInternalDac"] = caps.hasInternalDac;
    board["hasBle"] = caps.hasBle;
    board["hasWifi"] = caps.hasWifi;

    JsonArray reserved = board["reservedPins"].to<JsonArray>();
    for (uint8_t i = 0; i < caps.reservedPinCount; ++i) reserved.add(caps.reservedPins[i]);
    JsonArray inputOnly = board["inputOnlyPins"].to<JsonArray>();
    for (uint8_t i = 0; i < caps.inputOnlyPinCount; ++i) inputOnly.add(caps.inputOnlyPins[i]);
    JsonArray warn = board["warnPins"].to<JsonArray>();
    for (uint8_t i = 0; i < caps.warnPinCount; ++i) warn.add(caps.warnPins[i]);

    JsonArray backends = doc["audioBackends"].to<JsonArray>();
    for (uint8_t i = 0; i < audioBackendCount(); ++i) {
        const BackendDescriptor& d = audioBackendDescriptor(i);
        JsonObject o = backends.add<JsonObject>();
        o["key"] = d.key;
        o["title"] = d.title;
        o["summary"] = d.summary;
        o["maturity"] = toString(d.maturity);
        o["available"] = caps.supportsBackend(d.type);
        o["needsI2c"] = d.needsI2c;
        o["needsSdPin"] = d.needsSdPin;
        o["supportsCapture"] = d.supportsCapture;
        o["maxBitDepth"] = d.maxBitDepth;
    }

    static const AmplifierType kAmps[] = {AmplifierType::NONE, AmplifierType::MAX98357_INTERNAL,
                                          AmplifierType::TPA3118D2,
                                          AmplifierType::TAS5760_INTERNAL, AmplifierType::CUSTOM};
    JsonArray amps = doc["amplifiers"].to<JsonArray>();
    for (AmplifierType t : kAmps) {
        AmplifierConfig tmp;
        applyAmplifierDefaults(t, tmp);
        JsonObject o = amps.add<JsonObject>();
        o["key"] = toString(t);
        o["maxPower"] = tmp.maxPowerW;
        o["gainDb"] = tmp.gainDb;
        o["impedance"] = tmp.speakerImpedanceOhm;
    }

    static const SpeakerProfileId kSpeakers[] = {
        SpeakerProfileId::VISATON_FRS5_XTS, SpeakerProfileId::DAYTON_CE70PR4,
        SpeakerProfileId::VISATON_FRS8M, SpeakerProfileId::MONACOR_SPX30M,
        SpeakerProfileId::CUSTOM};
    JsonArray speakers = doc["speakers"].to<JsonArray>();
    for (SpeakerProfileId id : kSpeakers) {
        SpeakerConfig tmp;
        applySpeakerProfileDefaults(id, tmp);
        JsonObject o = speakers.add<JsonObject>();
        o["key"] = toString(id);
        o["name"] = tmp.name;
        o["impedance"] = tmp.impedanceOhm;
        o["powerRms"] = tmp.powerRmsW;
        o["minFrequency"] = tmp.minFrequencyHz;
        o["recommendedHighPass"] = tmp.recommendedHighPassHz;
        o["powerLimit"] = tmp.powerLimitW;
    }

    JsonArray presets = doc["presets"].to<JsonArray>();
    for (uint8_t i = 0; i < presetCount(); ++i) {
        const PresetInfo& p = presetInfo(i);
        JsonObject o = presets.add<JsonObject>();
        o["key"] = p.key;
        o["title"] = p.title;
        o["summary"] = p.summary;
        o["stars"] = p.stars;
        o["recommended"] = p.recommended;
        o["available"] = (!p.requiresNativeUsb || caps.hasNativeUsb) &&
                         (!p.requiresInternalDac || caps.hasInternalDac);
    }

    JsonObject midi = doc["midi"].to<JsonObject>();
    midi["usbAvailable"] = caps.hasNativeUsb;
    midi["bleAvailable"] = caps.hasBle;
    midi["rtpAvailable"] = caps.hasWifi;
    midi["dinAvailable"] = true;

    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

// ---------------------------------------------------------------------------
// Live actions
// ---------------------------------------------------------------------------
void HttpServerModule::handlePanic() {
    app_->panic();
    sendOk();
}

void HttpServerModule::handleReleasePanic() {
    app_->releasePanic();
    sendOk();
}

void HttpServerModule::handleAudioTest() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    const char* type = doc["type"] | "tone";
    const float amplitude = doc["amplitude"] | 0.2f;
    const uint32_t duration = doc["durationMs"] | 1500;

    if (strEqualsI(type, "sweep")) {
        app_->audio().startSweep(doc["startHz"] | 100.0f, doc["endHz"] | 8000.0f, duration,
                                 amplitude);
    } else if (strEqualsI(type, "stop")) {
        app_->audio().stopTestSignal();
    } else {
        app_->audio().startTestTone(doc["frequency"] | 440.0f, amplitude, duration);
    }
    sendOk();
}

void HttpServerModule::handleAudioMute() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    const bool muted = doc["muted"] | true;
    app_->audio().setMuted(muted);
    if (app_->backend()) app_->backend()->mute(muted);
    sendOk();
}

void HttpServerModule::handleAudioVolume() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    if (!doc["volume"].is<float>() && !doc["volume"].is<int>()) {
        sendError(400, "expected {\"volume\":0.0..1.0}");
        return;
    }
    app_->audio().setMasterVolume(doc["volume"].as<float>());
    sendOk();
}

void HttpServerModule::handleValveTest() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    const uint8_t valve = doc["valve"] | 0;
    const uint16_t duration = doc["durationMs"] | 300;
    if (!app_->valves().testPulse(valve, duration)) {
        sendError(400, "no driver is bound to this valve");
        return;
    }
    sendOk();
}

void HttpServerModule::handleValveMode() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    const char* mode = doc["mode"] | "";
    ValveMode parsed;
    if (!parseEnum(mode, parsed)) {
        sendError(400, "expected AUTO, MANUAL, MIDI_CC or DISABLED");
        return;
    }
    app_->valves().setMode(parsed);
    sendOk();
}

void HttpServerModule::handleValveManual() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    const uint8_t valve = doc["valve"] | 0;
    const bool pressed = doc["pressed"] | false;
    if (!app_->valves().manualSet(valve, pressed)) {
        sendError(409, "the valve engine is not in MANUAL mode");
        return;
    }
    sendOk();
}

void HttpServerModule::handleValveCalibrate() {
    JsonDocument doc;
    deserializeJson(doc, g_http->arg("plain"));
    const uint8_t valve = doc["valve"] | 0;
    const uint16_t angle = doc["angle"] | 90;
    if (angle > 180) {
        sendError(400, "the angle must stay within 0..180 degrees");
        return;
    }
    if (!app_->valves().calibrationPreview(valve, angle)) {
        sendError(400, "this valve is not a servo");
        return;
    }
    sendOk();
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------
void HttpServerModule::handleMidiStatus() {
    const MidiConfig& cfg = app_->config().midi;
    const MidiRouterStats& stats = app_->router().stats();

    JsonDocument doc;
    doc["ok"] = true;
    JsonArray ports = doc["ports"].to<JsonArray>();
    struct PortRow {
        MidiPort port;
        bool inEnabled;
        bool outEnabled;
        bool connected;
    };
    const PortRow rows[] = {
        {MidiPort::USB, cfg.usb.inEnabled, cfg.usb.outEnabled, app_->usbMidi().isConnected()},
        {MidiPort::BLE, cfg.ble.inEnabled, cfg.ble.outEnabled, app_->bleMidi().isConnected()},
        {MidiPort::RTP, cfg.rtp.inEnabled, cfg.rtp.outEnabled, app_->rtpMidi().isConnected()},
        {MidiPort::DIN, cfg.din.inEnabled, cfg.din.outEnabled, app_->dinMidi().isConnected()},
        {MidiPort::WEB, cfg.web.inEnabled, cfg.web.monitorEnabled,
         app_->webMidi().isConnected()},
    };
    for (const PortRow& r : rows) {
        JsonObject o = ports.add<JsonObject>();
        const uint8_t index = static_cast<uint8_t>(r.port);
        o["port"] = toString(r.port);
        o["in"] = r.inEnabled;
        o["out"] = r.outEnabled;
        o["connected"] = r.connected;
        o["rx"] = stats.rxPerPort[index];
        o["tx"] = stats.txPerPort[index];
    }

    JsonArray routes = doc["routes"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg.routeCount; ++i) {
        const MidiRoute& r = cfg.routes[i];
        JsonObject o = routes.add<JsonObject>();
        o["source"] = toString(r.source);
        o["destination"] = toString(r.destination);
        o["enabled"] = r.enabled;
        o["channelMask"] = r.channelMask;
        o["transpose"] = r.transpose;
        o["velocityCurve"] = toString(r.velocityCurve);
        o["noteMin"] = r.noteMin;
        o["noteMax"] = r.noteMax;
    }

    doc["rtpPeer"] = app_->rtpMidi().peerName();
    doc["droppedByFilter"] = stats.droppedByFilter;
    doc["loopsSuppressed"] = stats.loopsSuppressed;

    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handleMidiMonitor() {
    MidiMonitor& monitor = app_->monitor();
    JsonDocument doc;
    doc["ok"] = true;
    doc["paused"] = monitor.paused();
    doc["overflow"] = monitor.overflowCount();
    doc["perSecond"] = monitor.messagesPerSecond();
    JsonArray items = doc["items"].to<JsonArray>();
    // The buffer is capped at kMonitorCapacity, so the response size is bounded
    // whatever happens on the wire.
    for (uint16_t i = 0; i < monitor.count(); ++i) {
        const MonitorEntry& e = monitor.entry(i);
        JsonObject o = items.add<JsonObject>();
        o["ts"] = e.timestampMs;
        o["src"] = toString(e.source);
        o["type"] = MidiMonitor::typeName(e.type);
        o["ch"] = e.channel;
        o["d1"] = e.data1;
        o["d2"] = e.data2;
    }
    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

// ---------------------------------------------------------------------------
// Fingering table
// ---------------------------------------------------------------------------
void HttpServerModule::handleGetFingering() {
    const FingeringEngine& engine = app_->valves().fingering();
    JsonDocument doc;
    doc["ok"] = true;
    doc["transpose"] = engine.transposeSemitones();
    doc["pitchMode"] = toString(engine.config().pitchMode);
    doc["instrument"] = toString(engine.config().type);
    doc["valveCount"] = app_->valves().valveCount();

    JsonArray notes = doc["notes"].to<JsonArray>();
    // Only the practical range is sent: 128 rows would be mostly empty and the
    // table would be unusable in a browser.
    for (int note = 40; note <= 96; ++note) {
        const uint8_t primary = engine.primary(static_cast<uint8_t>(note));
        JsonObject o = notes.add<JsonObject>();
        o["written"] = note;
        o["primary"] = primary == kNoFingering ? -1 : primary;
        const uint8_t alt = engine.alternate(static_cast<uint8_t>(note));
        o["alternate"] = alt == kNoFingering ? -1 : alt;
        o["inRange"] = FingeringEngine::inStandardRange(note);
    }
    const size_t n = serializeJson(doc, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handlePutFingering() {
    JsonDocument doc;
    if (deserializeJson(doc, g_http->arg("plain")) != DeserializationError::Ok) {
        sendError(400, "expected {\"notes\":[{\"written\":60,\"primary\":0,\"alternate\":-1}]}");
        return;
    }
    FingeringEngine& engine = app_->valves().fingering();
    uint16_t applied = 0;
    for (JsonObjectConst o : doc["notes"].as<JsonArrayConst>()) {
        const int written = o["written"] | -1;
        if (written < 0 || written > 127) continue;
        const int primary = o["primary"] | -1;
        const int alternate = o["alternate"] | -1;
        if (engine.setFingering(static_cast<uint8_t>(written),
                                primary < 0 ? kNoFingering : static_cast<uint8_t>(primary),
                                alternate < 0 ? kNoFingering
                                              : static_cast<uint8_t>(alternate))) {
            ++applied;
        }
    }
    JsonDocument out;
    out["ok"] = true;
    out["applied"] = applied;
    // The table lives in RAM: it is rebuilt from the defaults plus the stored
    // overrides at every boot.  Persisting it is a schema v3 item, and the UI
    // says so rather than pretending the change survives a reset.
    out["persisted"] = false;
    const size_t n = serializeJson(out, g_buffer, sizeof(g_buffer));
    sendJson(200, g_buffer, n);
}

void HttpServerModule::handleResetFingering() {
    app_->valves().fingering().resetToDefault();
    sendOk();
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------
void HttpServerModule::handleReboot() {
    sendOk();
    app_->reboot(600);
}

void HttpServerModule::handleFactoryReset() {
    sendOk();
    app_->factoryReset();
}

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
void HttpServerModule::handleOtaUpload() {
    HTTPUpload& upload = g_http->upload();

    if (upload.status == UPLOAD_FILE_START) {
        otaError_ = false;
        if (!otaAvailable()) {
            otaError_ = true;
            OT_LOGE("ota", "no second application slot in this partition table");
            return;
        }
        // Park the instrument before touching the flash: audio muted, valves
        // released, solenoids de-energised.
        app_->panic();
        SystemState::instance().setMode(RunMode::UPDATING);
        OT_LOGW("ota", "update started (%s)", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            otaError_ = true;
            OT_LOGE("ota", "Update.begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE && !otaError_) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaError_ = true;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!otaError_ && Update.end(true)) {
            OT_LOGI("ota", "update complete, %u bytes", static_cast<unsigned>(upload.totalSize));
        } else {
            otaError_ = true;
            OT_LOGE("ota", "update failed");
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        otaError_ = true;
    }
}

void HttpServerModule::handleOtaFinished() {
    if (!otaAvailable()) {
        sendError(501,
                  "this build has a single application partition: OTA is not possible, flash "
                  "over USB instead");
        return;
    }
    if (otaError_ || Update.hasError()) {
        SystemState::instance().setMode(RunMode::SAFE_MODE);
        sendError(500, "the firmware update failed; the instrument stays on the old image");
        return;
    }
    sendOk();
    app_->reboot(800);
}

#else  // ------------------------------------------------------- host build

void HttpServerModule::registerRoutes() {}
bool HttpServerModule::serveStatic(const char*) { return false; }
void HttpServerModule::handleStatus() {}
void HttpServerModule::handleGetConfig() {}
void HttpServerModule::handlePutConfig() {}
void HttpServerModule::handleHardware() {}
void HttpServerModule::handlePanic() {}
void HttpServerModule::handleReleasePanic() {}
void HttpServerModule::handleAudioTest() {}
void HttpServerModule::handleAudioMute() {}
void HttpServerModule::handleAudioVolume() {}
void HttpServerModule::handleValveTest() {}
void HttpServerModule::handleValveMode() {}
void HttpServerModule::handleValveManual() {}
void HttpServerModule::handleValveCalibrate() {}
void HttpServerModule::handleMidiStatus() {}
void HttpServerModule::handleMidiMonitor() {}
void HttpServerModule::handleExport() {}
void HttpServerModule::handleImport() {}
void HttpServerModule::handlePreset() {}
void HttpServerModule::handleGetFingering() {}
void HttpServerModule::handlePutFingering() {}
void HttpServerModule::handleResetFingering() {}
void HttpServerModule::handleDiagnostics() {}
void HttpServerModule::handleReboot() {}
void HttpServerModule::handleFactoryReset() {}
void HttpServerModule::handleValidate() {}
void HttpServerModule::handleNotFound() {}
void HttpServerModule::handleOtaFinished() {}
void HttpServerModule::handleOtaUpload() {}

#endif

}  // namespace ot
