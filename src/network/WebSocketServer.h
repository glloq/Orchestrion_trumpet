// ============================================================================
//  WebSocketServer.h - the live channel between the browser and the trumpet.
//
//  Everything that must be immediate travels here and never over HTTP:
//    browser -> trumpet : note on/off, CC, pitch bend, valve commands, panic
//    trumpet -> browser : telemetry, valve states, MIDI monitor, audio state
//
//  Runs on port 81 in the network task, at a priority below audio, MIDI and
//  the actuators, so a chatty browser can never disturb the instrument.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class AppController;

class WebSocketServerModule {
public:
    bool begin(AppController* app);
    void end();
    void loop();

    uint8_t clientCount() const { return clientCount_; }
    void broadcastText(const char* payload, size_t length);

    // Called by the WebSockets library callback.
    void handleEvent(uint8_t clientId, int type, const uint8_t* payload, size_t length);

private:
    void handleTextMessage(uint8_t clientId, const char* text, size_t length);
    void pushTelemetry();
    void pushMonitor();

    AppController* app_ = nullptr;
    bool started_ = false;
    uint8_t clientCount_ = 0;
    uint32_t lastTelemetryMs_ = 0;
    uint32_t lastMonitorMs_ = 0;
    uint16_t monitorCursor_ = 0;
};

WebSocketServerModule& webSocketServer();

}  // namespace ot
