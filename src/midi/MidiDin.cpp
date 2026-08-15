#include "midi/MidiDin.h"

#include "diagnostics/Logger.h"

namespace ot {

namespace {
constexpr uint32_t kMidiBaud = 31250;
// Bytes drained per poll(): bounded so a flood on the wire can never starve
// the other work of the MIDI task.
constexpr uint8_t kMaxBytesPerPoll = 64;
}  // namespace

void MidiDinTransport::configure(const MidiDinConfig& cfg) { cfg_ = cfg; }

bool MidiDinTransport::begin() {
#if defined(OT_HOST_BUILD)
    started_ = true;
    return true;
#else
    if (!cfg_.inEnabled && !cfg_.outEnabled && !cfg_.thruEnabled) return false;

    switch (cfg_.uartNum) {
        case 0:
            uart_ = &Serial;
            break;
        case 1:
            uart_ = &Serial1;
            break;
        default:
            uart_ = &Serial2;
            break;
    }
    const int8_t rx = cfg_.inEnabled ? cfg_.rxGpio : -1;
    const int8_t tx = (cfg_.outEnabled || cfg_.thruEnabled) ? cfg_.txGpio : -1;
    uart_->begin(kMidiBaud, SERIAL_8N1, rx, tx);
    parser_.reset();
    started_ = true;
    OT_LOGI("din", "DIN MIDI on UART%u (RX %d, TX %d)", cfg_.uartNum, rx, tx);
    return true;
#endif
}

void MidiDinTransport::end() {
#if !defined(OT_HOST_BUILD)
    if (uart_ && cfg_.uartNum != 0) uart_->end();
#endif
    started_ = false;
}

void MidiDinTransport::poll() {
#if !defined(OT_HOST_BUILD)
    if (!started_ || !uart_ || !cfg_.inEnabled) return;
    uint8_t budget = kMaxBytesPerPoll;
    while (budget-- && uart_->available() > 0) {
        const uint8_t byte = static_cast<uint8_t>(uart_->read());
        ++rxBytes_;
        // THRU is byte level and happens before any parsing, so it stays
        // transparent even for messages this firmware does not understand.
        if (cfg_.thruEnabled) {
            uart_->write(byte);
            ++txBytes_;
        }
        MidiMessage msg;
        if (parser_.parse(byte, msg)) deliver(msg);
    }
#endif
}

void MidiDinTransport::onMidi(const MidiMessage& msg) {
#if !defined(OT_HOST_BUILD)
    if (!started_ || !uart_ || !cfg_.outEnabled) return;
    uint8_t bytes[3];
    const uint8_t n = msg.toBytes(bytes);
    if (n == 0) {
        // SysEx is forwarded byte by byte with its framing restored.
        if (msg.type == MidiType::SystemExclusive && msg.sysex && msg.sysexLength) {
            uart_->write(static_cast<uint8_t>(0xF0));
            uart_->write(msg.sysex, msg.sysexLength);
            uart_->write(static_cast<uint8_t>(0xF7));
            txBytes_ += msg.sysexLength + 2;
        }
        return;
    }
    uart_->write(bytes, n);
    txBytes_ += n;
#else
    (void)msg;
#endif
}

}  // namespace ot
