#include "network/WebSocketServer.h"

#include <ArduinoJson.h>

#include "core/AppController.h"
#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <WebSocketsServer.h>
#endif

namespace ot {

namespace {
constexpr uint16_t kPort = 81;
constexpr uint32_t kTelemetryIntervalMs = 200;
constexpr uint32_t kMonitorIntervalMs = 120;

#if !defined(OT_HOST_BUILD)
WebSocketsServer* g_server = nullptr;

void onWebSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
    webSocketServer().handleEvent(clientId, static_cast<int>(type), payload, length);
}
#endif
}  // namespace

WebSocketServerModule& webSocketServer() {
    static WebSocketServerModule module;
    return module;
}

bool WebSocketServerModule::begin(AppController* app) {
    app_ = app;
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (started_) return true;
    g_server = new WebSocketsServer(kPort);
    g_server->begin();
    g_server->onEvent(onWebSocketEvent);
    started_ = true;
    OT_LOGI("ws", "WebSocket server on port %u", kPort);
    return true;
#endif
}

void WebSocketServerModule::end() {
#if !defined(OT_HOST_BUILD)
    if (g_server) {
        g_server->close();
        delete g_server;
        g_server = nullptr;
    }
#endif
    started_ = false;
    clientCount_ = 0;
}

void WebSocketServerModule::broadcastText(const char* payload, size_t length) {
#if !defined(OT_HOST_BUILD)
    if (!g_server || clientCount_ == 0) return;
    g_server->broadcastTXT(reinterpret_cast<const uint8_t*>(payload), length);
#else
    (void)payload;
    (void)length;
#endif
}

void WebSocketServerModule::handleEvent(uint8_t clientId, int type, const uint8_t* payload,
                                        size_t length) {
#if !defined(OT_HOST_BUILD)
    switch (static_cast<WStype_t>(type)) {
        case WStype_CONNECTED:
            if (clientCount_ < 255) ++clientCount_;
            break;
        case WStype_DISCONNECTED:
            if (clientCount_ > 0) --clientCount_;
            break;
        case WStype_TEXT:
            handleTextMessage(clientId, reinterpret_cast<const char*>(payload), length);
            break;
        default:
            break;
    }
    if (app_) {
        app_->webMidi().setClientCount(clientCount_);
        SystemState::instance().mutableStatus().webClients = clientCount_;
    }
#else
    (void)clientId;
    (void)type;
    (void)payload;
    (void)length;
#endif
}

void WebSocketServerModule::handleTextMessage(uint8_t clientId, const char* text, size_t length) {
    if (!app_ || !text || length == 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, text, length) != DeserializationError::Ok) return;
    const char* command = doc["t"];
    if (!command) return;

    const uint8_t channel = doc["c"].is<int>() ? doc["c"].as<uint8_t>() : 1;

    if (strEqualsI(command, "noteon")) {
        MidiMessage m = MidiMessage::noteOn(channel, doc["n"] | 60, doc["v"] | 100);
        app_->webMidi().injectFromWeb(m);
    } else if (strEqualsI(command, "noteoff")) {
        MidiMessage m = MidiMessage::noteOff(channel, doc["n"] | 60, doc["v"] | 0);
        app_->webMidi().injectFromWeb(m);
    } else if (strEqualsI(command, "cc")) {
        MidiMessage m = MidiMessage::controlChange(channel, doc["n"] | 1, doc["v"] | 0);
        app_->webMidi().injectFromWeb(m);
    } else if (strEqualsI(command, "pb")) {
        MidiMessage m = MidiMessage::pitchBend(channel, doc["v"] | 0);
        app_->webMidi().injectFromWeb(m);
    } else if (strEqualsI(command, "panic")) {
        app_->panic();
    } else if (strEqualsI(command, "valve")) {
        // Manual valve command from the Pistons page.
        const uint8_t index = doc["i"] | 0;
        const bool pressed = doc["p"] | false;
        app_->valves().manualSet(index, pressed);
    } else if (strEqualsI(command, "monitor")) {
        app_->monitor().setPaused(doc["paused"] | false);
        if (doc["clear"] | false) {
            app_->monitor().clear();
            monitorCursor_ = 0;
        }
    }
    (void)clientId;
}

void WebSocketServerModule::pushTelemetry() {
    if (!app_ || clientCount_ == 0) return;

    const SystemStatus& s = SystemState::instance().status();
    const AudioEngineStatus a = app_->audio().status();

    JsonDocument doc;
    doc["t"] = "telemetry";
    doc["mode"] = SystemState::toString(s.mode);
    doc["uptime"] = SystemState::instance().uptimeSeconds();

    JsonObject audio = doc["audio"].to<JsonObject>();
    audio["running"] = s.audioRunning;
    audio["muted"] = s.audioMuted;
    audio["underruns"] = s.audioUnderruns;
    audio["cpu"] = s.audioCpuPercent;
    audio["peak"] = a.peakLevel;
    audio["gainReduction"] = a.gainReduction;
    audio["note"] = a.noteActive ? a.note : 0;
    audio["velocity"] = a.velocity;
    audio["frequency"] = a.frequencyHz;
    audio["backend"] = app_->backend() ? app_->backend()->name() : "NONE";
    audio["sampleRate"] = app_->backend() ? app_->backend()->sampleRate() : 0;
    audio["volume"] = app_->audio().masterVolume();

    JsonObject midi = doc["midi"].to<JsonObject>();
    midi["rx"] = s.midiRxPerSecond;
    midi["tx"] = s.midiTxPerSecond;
    midi["usb"] = app_->usbMidi().isConnected();
    midi["ble"] = app_->bleMidi().isConnected();
    midi["din"] = app_->dinMidi().isConnected();
    midi["rtp"] = app_->rtpMidi().isConnected();
    midi["web"] = clientCount_;

    JsonArray valves = doc["valves"].to<JsonArray>();
    for (uint8_t i = 0; i < app_->valves().valveCount(); ++i) {
        const ValveStatus v = app_->valves().status(i);
        JsonObject o = valves.add<JsonObject>();
        o["pressed"] = v.pressed;
        o["type"] = toString(v.type);
        o["angle"] = v.angle;
        o["duty"] = v.dutyPercent;
        o["fault"] = v.fault;
        if (v.faultText) o["faultText"] = v.faultText;
    }
    doc["valveMode"] = toString(app_->valves().mode());
    doc["faults"] = s.faultMask;
    if (s.faultText[0]) doc["faultText"] = s.faultText;

    char buffer[1024];
    const size_t n = serializeJson(doc, buffer, sizeof(buffer));
    broadcastText(buffer, n);
}

void WebSocketServerModule::pushMonitor() {
    if (!app_ || clientCount_ == 0) return;
    MidiMonitor& monitor = app_->monitor();
    if (monitor.count() == 0) return;

    // Only send what the browser has not seen yet, and at most a handful per
    // push: the monitor must never become the reason the network task is busy.
    if (monitorCursor_ > monitor.count()) monitorCursor_ = 0;
    if (monitorCursor_ >= monitor.count()) return;

    JsonDocument doc;
    doc["t"] = "monitor";
    JsonArray items = doc["items"].to<JsonArray>();
    uint8_t sent = 0;
    while (monitorCursor_ < monitor.count() && sent < 12) {
        const MonitorEntry& e = monitor.entry(monitorCursor_++);
        JsonObject o = items.add<JsonObject>();
        o["ts"] = e.timestampMs;
        o["src"] = toString(e.source);
        o["type"] = MidiMonitor::typeName(e.type);
        o["ch"] = e.channel;
        o["d1"] = e.data1;
        o["d2"] = e.data2;
        ++sent;
    }
    if (sent == 0) return;

    char buffer[1024];
    const size_t n = serializeJson(doc, buffer, sizeof(buffer));
    broadcastText(buffer, n);
}

void WebSocketServerModule::loop() {
#if !defined(OT_HOST_BUILD)
    if (!started_ || !g_server) return;
    g_server->loop();

    // Outgoing MIDI routed to the WEB port (monitor destination, echo of a
    // route) is forwarded to the browsers.
    if (app_) {
        MidiMessage msg;
        uint8_t budget = 8;
        while (budget-- && app_->webMidi().popOutgoing(msg)) {
            JsonDocument doc;
            doc["t"] = "midi";
            doc["type"] = MidiMonitor::typeName(msg.type);
            doc["ch"] = msg.channel;
            doc["d1"] = msg.data1;
            doc["d2"] = msg.data2;
            char buffer[160];
            const size_t n = serializeJson(doc, buffer, sizeof(buffer));
            broadcastText(buffer, n);
        }
    }

    const uint32_t now = millis();
    if (now - lastTelemetryMs_ >= kTelemetryIntervalMs) {
        lastTelemetryMs_ = now;
        pushTelemetry();
    }
    if (now - lastMonitorMs_ >= kMonitorIntervalMs) {
        lastMonitorMs_ = now;
        pushMonitor();
    }
#endif
}

}  // namespace ot
