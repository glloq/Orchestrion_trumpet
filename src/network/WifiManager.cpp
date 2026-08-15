#include "network/WifiManager.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

#include <cstdio>

namespace ot {

namespace {
// How long we wait for a station join before falling back to the hotspot.
constexpr uint32_t kStaJoinTimeoutMs = 12000;
// How often a lost station connection is retried.
constexpr uint32_t kRetryIntervalMs = 30000;
}  // namespace

void WifiManager::defaultApSsid(char* out, size_t size) {
#if defined(OT_HOST_BUILD)
    copyString(out, size, "MIDI-Trumpet-TEST");
#else
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    snprintf(out, size, "MIDI-Trumpet-%02X%02X", mac[4], mac[5]);
#endif
}

void WifiManager::configure(const WifiConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.apSsid[0]) {
        copyString(apSsid_, sizeof(apSsid_), cfg_.apSsid);
    } else {
        defaultApSsid(apSsid_, sizeof(apSsid_));
    }
}

bool WifiManager::startAp() {
#if defined(OT_HOST_BUILD)
    apActive_ = true;
    return true;
#else
    const bool open = cfg_.apPassword[0] == '\0';
    // An open network is deliberate: the very first configuration must be
    // reachable with no shared secret.  The wizard offers to set one.
    const bool ok = WiFi.softAP(apSsid_, open ? nullptr : cfg_.apPassword, cfg_.apChannel);
    apActive_ = ok;
    if (ok) {
        OT_LOGI("wifi", "hotspot \"%s\" up on %s", apSsid_, WiFi.softAPIP().toString().c_str());
    } else {
        OT_LOGE("wifi", "could not start the hotspot");
    }
    return ok;
#endif
}

bool WifiManager::startSta() {
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (cfg_.ssid[0] == '\0') return false;
    WiFi.setHostname(cfg_.hostname);
    WiFi.begin(cfg_.ssid, cfg_.password);
    staStartedMs_ = millis();
    staPending_ = true;
    OT_LOGI("wifi", "joining \"%s\"", cfg_.ssid);
    return true;
#endif
}

bool WifiManager::begin() {
#if defined(OT_HOST_BUILD)
    started_ = true;
    return true;
#else
    started_ = true;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    switch (cfg_.mode) {
        case WifiMode::STA:
            WiFi.mode(WIFI_STA);
            if (!startSta()) {
                // Nothing configured: never leave the user without an interface.
                OT_LOGW("wifi", "STA requested but no SSID stored, falling back to AP");
                WiFi.mode(WIFI_AP);
                startAp();
            }
            break;
        case WifiMode::AP_STA:
            WiFi.mode(WIFI_AP_STA);
            startAp();
            startSta();
            break;
        case WifiMode::AP:
        default:
            WiFi.mode(WIFI_AP);
            startAp();
            break;
    }
    refreshIp();
    return apActive_ || staPending_;
#endif
}

void WifiManager::end() {
#if !defined(OT_HOST_BUILD)
    if (mdns_) {
        MDNS.end();
        mdns_ = false;
    }
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
    started_ = false;
    apActive_ = false;
    staConnected_ = false;
}

void WifiManager::refreshIp() {
#if !defined(OT_HOST_BUILD)
    if (staConnected_) {
        copyString(ip_, sizeof(ip_), WiFi.localIP().toString().c_str());
    } else if (apActive_) {
        copyString(ip_, sizeof(ip_), WiFi.softAPIP().toString().c_str());
    } else {
        copyString(ip_, sizeof(ip_), "0.0.0.0");
    }
#else
    copyString(ip_, sizeof(ip_), "192.168.4.1");
#endif
}

void WifiManager::loop() {
#if !defined(OT_HOST_BUILD)
    if (!started_) return;
    const uint32_t now = millis();
    const bool connected = WiFi.status() == WL_CONNECTED;

    if (connected != staConnected_) {
        staConnected_ = connected;
        staPending_ = false;
        refreshIp();
        if (connected) {
            OT_LOGI("wifi", "connected, IP %s", ip_);
            if (!mdns_ && MDNS.begin(cfg_.hostname)) {
                MDNS.addService("http", "tcp", 80);
                // Advertise the RTP-MIDI session so a DAW can discover the
                // instrument without typing an IP address.
                MDNS.addService("apple-midi", "udp", 5004);
                mdns_ = true;
                OT_LOGI("wifi", "reachable as http://%s.local", cfg_.hostname);
            }
        } else {
            OT_LOGW("wifi", "station connection lost");
        }
    }

    // Fallback: a station-only configuration that cannot join must still let
    // the user in through the hotspot.
    if (staPending_ && !connected && (now - staStartedMs_) > kStaJoinTimeoutMs) {
        staPending_ = false;
        if (!apActive_) {
            OT_LOGW("wifi", "could not join \"%s\", falling back to the hotspot", cfg_.ssid);
            WiFi.mode(WIFI_AP_STA);
            startAp();
            refreshIp();
        }
        lastAttemptMs_ = now;
    }

    if (!connected && !staPending_ && cfg_.ssid[0] && cfg_.mode != WifiMode::AP &&
        (now - lastAttemptMs_) > kRetryIntervalMs) {
        lastAttemptMs_ = now;
        startSta();
    }
#endif
}

int8_t WifiManager::rssi() const {
#if defined(OT_HOST_BUILD)
    return 0;
#else
    return staConnected_ ? static_cast<int8_t>(WiFi.RSSI()) : 0;
#endif
}

}  // namespace ot
