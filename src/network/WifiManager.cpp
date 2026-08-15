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
// Settle window between WiFi.mode(WIFI_AP) and softAP().  The classic ESP32
// needs it; doing it as a delay would have held up the whole network task.
constexpr uint32_t kApSettleMs = 120;
// WPA2 refuses anything shorter, and silently produces an open network.
constexpr size_t kMinApPasswordLength = 8;
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

bool WifiManager::accessPointSecured() const {
    return apActive_ && strlen(cfg_.apPassword) >= kMinApPasswordLength;
}

// ---------------------------------------------------------------------------
// Access point, brought up in two non-blocking steps
// ---------------------------------------------------------------------------
void WifiManager::requestAp() {
    if (apActive_ || apPending_) return;
#if !defined(OT_HOST_BUILD)
    // Keep the station interface alive when one is configured, so a hotspot
    // opened on demand does not tear down a working network connection.
    WiFi.mode(cfg_.ssid[0] ? WIFI_AP_STA : WIFI_AP);
#endif
    apPending_ = true;
    apPendingSinceMs_ = OT_MILLIS();
}

bool WifiManager::finishAp() {
    apPending_ = false;
#if defined(OT_HOST_BUILD)
    apActive_ = true;
    return true;
#else
    // A password shorter than the WPA2 minimum would give an open network with
    // no warning, so it is treated as "no password" and said out loud.
    const size_t passwordLength = strlen(cfg_.apPassword);
    const bool secured = passwordLength >= kMinApPasswordLength;
    if (passwordLength > 0 && !secured) {
        OT_LOGW("wifi", "the hotspot password is shorter than %u characters: opening it instead",
                static_cast<unsigned>(kMinApPasswordLength));
    }

    const bool ok = WiFi.softAP(apSsid_, secured ? cfg_.apPassword : nullptr, cfg_.apChannel);
    apActive_ = ok;
    if (ok) {
        OT_LOGI("wifi", "hotspot \"%s\" up on %s (%s)", apSsid_,
                WiFi.softAPIP().toString().c_str(), secured ? "WPA2" : "open");
    } else {
        OT_LOGE("wifi", "could not start the hotspot");
    }
    refreshIp();
    return ok;
#endif
}

void WifiManager::forceAccessPoint() {
    // Latched: once the user has asked for the hotspot, the station is not
    // retried until the next reboot.  Otherwise a half-working network would
    // pull the interface away again while they are typing new credentials.
    apForced_ = true;
    staPending_ = false;
    OT_LOGI("wifi", "hotspot forced on demand");
    requestAp();
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
                requestAp();
            }
            break;
        case WifiMode::AP_STA:
            requestAp();
            startSta();
            break;
        case WifiMode::AP:
        default:
            requestAp();
            break;
    }
    refreshIp();
    return true;
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
    apPending_ = false;
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

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------
bool WifiManager::startScan() {
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (scanning_) return false;
    // Scanning needs the station interface.  When the hotspot is up we move to
    // AP+STA so the browser session that asked for the scan survives it.
    if (apActive_) WiFi.mode(WIFI_AP_STA);
    if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) return false;
    scanning_ = true;
    return true;
#endif
}

void WifiManager::harvestScan() {
#if !defined(OT_HOST_BUILD)
    if (!scanning_) return;
    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;

    scanCount_ = 0;
    if (result > 0) {
        for (int16_t i = 0; i < result && scanCount_ < kMaxScanResults; ++i) {
            const String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue;   // hidden network: nothing to show

            // Keep the strongest entry per SSID: a mesh shows the same name on
            // several channels and the list would otherwise be mostly noise.
            bool duplicate = false;
            for (uint8_t k = 0; k < scanCount_; ++k) {
                if (ssid == scanResults_[k].ssid) {
                    duplicate = true;
                    if (WiFi.RSSI(i) > scanResults_[k].rssi) {
                        scanResults_[k].rssi = static_cast<int8_t>(WiFi.RSSI(i));
                        scanResults_[k].channel = static_cast<uint8_t>(WiFi.channel(i));
                    }
                    break;
                }
            }
            if (duplicate) continue;

            WifiScanEntry& e = scanResults_[scanCount_++];
            copyString(e.ssid, sizeof(e.ssid), ssid.c_str());
            e.rssi = static_cast<int8_t>(WiFi.RSSI(i));
            e.channel = static_cast<uint8_t>(WiFi.channel(i));
            e.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        // Strongest first: the network the user wants is almost always on top.
        for (uint8_t i = 1; i < scanCount_; ++i) {
            WifiScanEntry key = scanResults_[i];
            int8_t j = static_cast<int8_t>(i) - 1;
            while (j >= 0 && scanResults_[j].rssi < key.rssi) {
                scanResults_[j + 1] = scanResults_[j];
                --j;
            }
            scanResults_[j + 1] = key;
        }
    }
    WiFi.scanDelete();
    scanning_ = false;
    ++scanGeneration_;
    OT_LOGI("wifi", "scan finished: %u network(s)", scanCount_);
#endif
}

// ---------------------------------------------------------------------------
// Periodic work
// ---------------------------------------------------------------------------
void WifiManager::loop() {
#if !defined(OT_HOST_BUILD)
    if (!started_) return;
    const uint32_t now = millis();

    if (apPending_ && (now - apPendingSinceMs_) >= kApSettleMs) finishAp();
    harvestScan();

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
        if (!apActive_ && !apPending_) {
            OT_LOGW("wifi", "could not join \"%s\", falling back to the hotspot", cfg_.ssid);
            requestAp();
        }
        lastAttemptMs_ = now;
    }

    // A hotspot the user asked for explicitly is never taken away from them.
    if (!apForced_ && !connected && !staPending_ && cfg_.ssid[0] &&
        cfg_.mode != WifiMode::AP && (now - lastAttemptMs_) > kRetryIntervalMs) {
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
