#include "midi/MidiBle.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <soc/soc_caps.h>
#if SOC_BLE_SUPPORTED
#define OT_HAS_BLE 1
#include <BLE2902.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#endif
#endif

namespace ot {

namespace {
constexpr const char* kServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
constexpr const char* kCharacteristicUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

#if defined(OT_HAS_BLE)
BLECharacteristic* g_characteristic = nullptr;
BLEServer* g_server = nullptr;
MidiBleTransport* g_owner = nullptr;

class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        if (g_owner) g_owner->setConnected(true);
    }
    void onDisconnect(BLEServer* server) override {
        if (g_owner) g_owner->setConnected(false);
        // A BLE-MIDI device must always be discoverable again straight away,
        // otherwise the user has to power cycle the trumpet to reconnect.
        server->startAdvertising();
    }
};

class CharacteristicCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        if (!g_owner) return;
        g_owner->handleIncoming(characteristic->getData(), characteristic->getLength());
    }
};

ServerCallbacks g_serverCallbacks;
CharacteristicCallbacks g_charCallbacks;
#endif
}  // namespace

void MidiBleTransport::configure(const MidiBleConfig& cfg) { cfg_ = cfg; }

bool MidiBleTransport::begin() {
#if defined(OT_HAS_BLE)
    if (started_) return true;
    if (!cfg_.inEnabled && !cfg_.outEnabled) return false;

    g_owner = this;
    BLEDevice::init(cfg_.deviceName[0] ? cfg_.deviceName : "MIDI Trumpet");
    g_server = BLEDevice::createServer();
    g_server->setCallbacks(&g_serverCallbacks);

    BLEService* service = g_server->createService(BLEUUID(kServiceUuid));
    g_characteristic = service->createCharacteristic(
        BLEUUID(kCharacteristicUuid),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR |
            BLECharacteristic::PROPERTY_NOTIFY);
    g_characteristic->addDescriptor(new BLE2902());
    g_characteristic->setCallbacks(&g_charCallbacks);
    service->start();

    BLEAdvertising* advertising = g_server->getAdvertising();
    advertising->addServiceUUID(BLEUUID(kServiceUuid));
    advertising->setScanResponse(true);
    advertising->start();

    parser_.reset();
    started_ = true;
    OT_LOGI("ble", "BLE MIDI advertising as \"%s\"", cfg_.deviceName);
    return true;
#else
    OT_LOGW("ble", "this build has no BLE support");
    return false;
#endif
}

void MidiBleTransport::end() {
#if defined(OT_HAS_BLE)
    if (!started_) return;
    BLEDevice::deinit(false);
    g_characteristic = nullptr;
    g_server = nullptr;
    g_owner = nullptr;
#endif
    started_ = false;
    connected_ = false;
}

void MidiBleTransport::setConnected(bool state) {
    connected_ = state;
    if (!state) {
        parser_.reset();
        txLength_ = 0;
        txRunningStatus_ = 0;
    }
}

void MidiBleTransport::handleIncoming(const uint8_t* data, size_t length) {
    if (!cfg_.inEnabled || !data || length < 3) return;

    // BLE-MIDI framing: one header byte, then per-message timestamp bytes.
    // Both carry the high bit set, so they must be stripped before the bytes
    // reach the standard MIDI parser.
    for (size_t i = 1; i < length; ++i) {
        const uint8_t byte = data[i];
        // A timestamp-low byte (0x80..0xFF) always precedes a status byte and
        // never appears where a data byte is expected.
        if ((byte & 0x80) && (i + 1) < length && (data[i + 1] & 0x80)) continue;
        MidiMessage msg;
        if (parser_.parse(byte, msg)) deliver(msg);
    }
}

void MidiBleTransport::onMidi(const MidiMessage& msg) {
    if (!connected_ || !cfg_.outEnabled) return;
    uint8_t bytes[3];
    const uint8_t n = msg.toBytes(bytes);
    if (n == 0) return;   // SysEx over BLE is not implemented

    const uint32_t now = OT_MILLIS();
    const uint8_t timestampHigh = static_cast<uint8_t>(0x80 | ((now >> 7) & 0x3F));
    const uint8_t timestampLow = static_cast<uint8_t>(0x80 | (now & 0x7F));

    // Start a new packet when the current one cannot take this message.
    if (txLength_ == 0 || txLength_ + n + 1 > sizeof(txBuffer_)) {
        if (txLength_ > 0) flushOutput();
        txBuffer_[0] = timestampHigh;
        txLength_ = 1;
    }
    txBuffer_[txLength_++] = timestampLow;
    for (uint8_t i = 0; i < n; ++i) txBuffer_[txLength_++] = bytes[i];
}

void MidiBleTransport::flushOutput() {
#if defined(OT_HAS_BLE)
    if (txLength_ < 3 || !g_characteristic) {
        txLength_ = 0;
        return;
    }
    g_characteristic->setValue(txBuffer_, txLength_);
    g_characteristic->notify();
#endif
    txLength_ = 0;
}

void MidiBleTransport::poll() {
    // Outgoing messages are batched by onMidi() and released here, once per
    // MIDI task cycle: one notification instead of one per note.
    if (txLength_ > 0) flushOutput();
}

}  // namespace ot
