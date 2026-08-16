// ============================================================================
//  WebServer.h - static files + REST API + OTA.
//
//  The class is called HttpServerModule so it never collides with the Arduino
//  core's own `WebServer` type, which it uses internally.
//
//  Everything that is not time critical goes through here; anything live goes
//  through the WebSocket.  The server runs in the network task on core 0.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

class AppController;

class HttpServerModule {
public:
    bool begin(AppController* app);
    void end();
    void loop();
    bool started() const { return started_; }

private:
    void registerRoutes();

    // --- handlers ---
    void handleStatus();
    void handleGetConfig();
    void handlePutConfig();
    void handleHardware();
    void handlePanic();
    void handleReleasePanic();
    void handleAudioTest();
    void handleAudioMute();
    void handleAudioVolume();
    void handleAudioPreview();
    void handleAudioRevert();
    void handleAudioCommit();
    void handleVoicings();
    void handleVoicingSave();
    void handleVoicingLoad();
    void handleVoicingDelete();
    void handleValveTest();
    void handleValveMode();
    void handleValveManual();
    void handleValveCalibrate();
    void handleWifiScan();
    void handleWifiHotspot();
    void handleWifiCredentials();
    void handleMidiStatus();
    void handleMidiMonitor();
    void handleMidiMonitorControl();
    void handleMidiMonitorClear();
    void handleExport();
    void handleImport();
    void handlePreset();
    void handleGetFingering();
    void handlePutFingering();
    void handleResetFingering();
    void handleDiagnostics();
    void handleReboot();
    void handleFactoryReset();
    void handleValidate();
    void handleNotFound();
    void handleOtaFinished();
    void handleOtaUpload();

    bool serveStatic(const char* uri);
    void sendJson(int code, const char* json, size_t length);
    void sendError(int code, const char* message);
    void sendOk();

    AppController* app_ = nullptr;
    bool started_ = false;
    bool otaError_ = false;
};

HttpServerModule& webServer();

}  // namespace ot
