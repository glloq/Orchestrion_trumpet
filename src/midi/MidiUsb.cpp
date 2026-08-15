#include "midi/MidiUsb.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <Arduino.h>
#include <soc/soc_caps.h>
#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_TINYUSB_MIDI_ENABLED)
#define OT_HAS_USB_MIDI 1
#include <USB.h>
#include <USBMIDI.h>
#endif
#endif

namespace ot {

namespace {
#if defined(OT_HAS_USB_MIDI)
USBMIDI g_usbMidi;

// USB-MIDI event packets carry a 4 bit "code index number" that mirrors the
// message type; the header also holds the virtual cable number (always 0 here).
uint8_t codeIndexFor(MidiType type) {
    switch (type) {
        case MidiType::NoteOff:
            return 0x8;
        case MidiType::NoteOn:
            return 0x9;
        case MidiType::PolyPressure:
            return 0xA;
        case MidiType::ControlChange:
            return 0xB;
        case MidiType::ProgramChange:
            return 0xC;
        case MidiType::ChannelPressure:
            return 0xD;
        case MidiType::PitchBend:
            return 0xE;
        default:
            return 0xF;   // single byte / system common
    }
}
#endif
}  // namespace

bool MidiUsbTransport::platformSupportsUsbMidi() {
#if defined(OT_HAS_USB_MIDI)
    return true;
#else
    return false;
#endif
}

void MidiUsbTransport::configure(const MidiUsbConfig& cfg) { cfg_ = cfg; }

bool MidiUsbTransport::begin() {
#if defined(OT_HAS_USB_MIDI)
    if (started_) return true;
    if (!cfg_.inEnabled && !cfg_.outEnabled) return false;
    g_usbMidi.begin();
    USB.begin();
    available_ = true;
    started_ = true;
    OT_LOGI("usb", "class compliant USB-MIDI started");
    return true;
#else
    available_ = false;
    OT_LOGW("usb", "this board has no native USB: USB-MIDI is unavailable");
    return false;
#endif
}

void MidiUsbTransport::end() {
#if defined(OT_HAS_USB_MIDI)
    if (started_) g_usbMidi.end();
#endif
    started_ = false;
}

bool MidiUsbTransport::isConnected() const {
#if defined(OT_HAS_USB_MIDI)
    return started_ && USB;
#else
    return false;
#endif
}

void MidiUsbTransport::poll() {
#if defined(OT_HAS_USB_MIDI)
    if (!started_ || !cfg_.inEnabled) return;
    midiEventPacket_t packet;
    uint8_t budget = 32;   // bounded: never let USB starve the MIDI task
    while (budget-- && g_usbMidi.readPacket(&packet)) {
        const uint8_t status = packet.byte1;
        if (status < 0x80) continue;

        MidiMessage msg;
        msg.source = MidiPort::USB;
        msg.timestampMs = millis();
        if (status < 0xF0) {
            msg.type = static_cast<MidiType>(status & 0xF0);
            msg.channel = static_cast<uint8_t>((status & 0x0F) + 1);
        } else {
            msg.type = static_cast<MidiType>(status);
            msg.channel = 1;
        }
        msg.data1 = packet.byte2 & 0x7F;
        msg.data2 = packet.byte3 & 0x7F;
        if (msg.type == MidiType::SystemExclusive) continue;   // not forwarded yet
        deliver(msg);
    }
#endif
}

void MidiUsbTransport::onMidi(const MidiMessage& msg) {
#if defined(OT_HAS_USB_MIDI)
    if (!started_ || !cfg_.outEnabled) return;
    uint8_t bytes[3];
    const uint8_t n = msg.toBytes(bytes);
    if (n == 0) return;

    midiEventPacket_t packet;
    packet.header = codeIndexFor(msg.type);   // cable 0
    packet.byte1 = bytes[0];
    packet.byte2 = n > 1 ? bytes[1] : 0;
    packet.byte3 = n > 2 ? bytes[2] : 0;
    g_usbMidi.writePacket(&packet);
#else
    (void)msg;
#endif
}

}  // namespace ot
