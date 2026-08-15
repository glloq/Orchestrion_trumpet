// ============================================================================
//  WifiManager.h - hotspot first, station when configured.
//
//  Out of the box the instrument creates "MIDI-Trumpet-XXXX" (XXXX derived
//  from the ESP32 MAC) so the user can reach http://192.168.4.1 with nothing
//  but a phone.  When a station is configured the manager tries it and falls
//  back to the AP automatically if the join fails or is later lost.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

namespace ot {

// One nearby network, as reported by a scan.
struct WifiScanEntry {
    char ssid[kNameLen] = "";
    int8_t rssi = 0;
    uint8_t channel = 0;
    bool secured = false;
};

static constexpr uint8_t kMaxScanResults = 16;

class WifiManager {
public:
    void configure(const WifiConfig& cfg);
    bool begin();
    void end();
    // Called periodically by the network task: handles the fallback to AP, the
    // reconnection attempts, the deferred access point bring-up and the harvest
    // of a finished scan.  Never blocks.
    void loop();

    // Brings the hotspot up on demand and keeps it up until the next reboot, no
    // matter what the configured mode says.  This is the escape hatch when the
    // stored station credentials are wrong: reachable from the web UI and from
    // the board's BOOT button.
    void forceAccessPoint();
    bool accessPointForced() const { return apForced_; }

    // Asynchronous survey of nearby networks, so the user picks an SSID from a
    // list instead of typing it blind.  loop() harvests the result.
    bool startScan();
    bool scanInProgress() const { return scanning_; }
    uint8_t scanResultCount() const { return scanCount_; }
    const WifiScanEntry& scanResult(uint8_t index) const {
        return scanResults_[index < scanCount_ ? index : 0];
    }
    uint32_t scanGeneration() const { return scanGeneration_; }

    // True only when the hotspot is up AND protected: WPA2 needs 8 characters,
    // so a shorter password silently produces an OPEN network and the UI has to
    // be able to say so.
    bool accessPointSecured() const;

    bool apActive() const { return apActive_; }
    bool staConnected() const { return staConnected_; }
    const char* apSsid() const { return apSsid_; }
    const char* hostname() const { return cfg_.hostname; }
    int8_t rssi() const;
    // Textual IP of whichever interface the browser should use.
    const char* ipAddress() const { return ip_; }
    bool mdnsStarted() const { return mdns_; }

    // "MIDI-Trumpet-XXXX" from the MAC address.
    static void defaultApSsid(char* out, size_t size);

private:
    // The classic ESP32 wants a short settle between WiFi.mode(WIFI_AP) and
    // softAP().  That used to be a delay() reachable from loop(); it is a two
    // step sequence now so nothing in the network task ever blocks.
    void requestAp();
    bool finishAp();
    bool startSta();
    void refreshIp();
    void harvestScan();

    WifiConfig cfg_;
    char apSsid_[kNameLen] = "";
    char ip_[16] = "0.0.0.0";
    uint32_t lastAttemptMs_ = 0;
    uint32_t staStartedMs_ = 0;
    uint32_t apPendingSinceMs_ = 0;
    uint32_t scanGeneration_ = 0;
    bool apActive_ = false;
    bool apPending_ = false;
    bool apForced_ = false;
    bool staConnected_ = false;
    bool staPending_ = false;
    bool mdns_ = false;
    bool started_ = false;
    bool scanning_ = false;
    uint8_t scanCount_ = 0;
    WifiScanEntry scanResults_[kMaxScanResults];
};

}  // namespace ot
