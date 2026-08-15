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

class WifiManager {
public:
    void configure(const WifiConfig& cfg);
    bool begin();
    void end();
    // Called periodically by the network task: handles the fallback to AP and
    // the reconnection attempts.  Never blocks.
    void loop();

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
    bool startAp();
    bool startSta();
    void refreshIp();

    WifiConfig cfg_;
    char apSsid_[kNameLen] = "";
    char ip_[16] = "0.0.0.0";
    uint32_t lastAttemptMs_ = 0;
    uint32_t staStartedMs_ = 0;
    bool apActive_ = false;
    bool staConnected_ = false;
    bool staPending_ = false;
    bool mdns_ = false;
    bool started_ = false;
};

}  // namespace ot
