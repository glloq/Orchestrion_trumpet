#include "audio/AudioBackendFactory.h"

#include "audio/backends/Es8388.h"
#include "audio/backends/Esp32Dac.h"
#include "audio/backends/Max98357.h"
#include "audio/backends/NullBackend.h"
#include "audio/backends/Pcm5102.h"
#include "audio/backends/Tas5760.h"
#include "audio/backends/Wm8960.h"
#include "config/BoardCaps.h"

namespace ot {

const char* toString(BackendMaturity m) {
    switch (m) {
        case BackendMaturity::STABLE:
            return "STABLE";
        case BackendMaturity::PROTOTYPE:
            return "PROTOTYPE";
        case BackendMaturity::EXPERIMENTAL:
        default:
            return "EXPERIMENTAL";
    }
}

namespace {
const BackendDescriptor kBackends[] = {
    {AudioBackendType::NONE, "NONE", "No audio output",
     "Valves only. Also used by safe mode.", BackendMaturity::STABLE, false, false, false, 16},

    {AudioBackendType::ESP32_INTERNAL_DAC, "ESP32_INTERNAL_DAC", "ESP32 internal DAC",
     "8 bit, prototype only. Classic ESP32 (GPIO25/26).", BackendMaturity::PROTOTYPE, false,
     false, false, 8},

    {AudioBackendType::MAX98357A, "MAX98357A", "MAX98357A",
     "I2S class-D amplifier, speaker directly attached. 16 bit.", BackendMaturity::STABLE, false,
     true, false, 16},

    {AudioBackendType::PCM5102A, "PCM5102A", "PCM5102A",
     "Reference DAC. Line level into an external amplifier.", BackendMaturity::STABLE, false,
     true, false, 32},

    {AudioBackendType::ES8388, "ES8388", "ES8388 codec",
     "DAC + ADC. Microphone input reserved for acoustic calibration.",
     BackendMaturity::EXPERIMENTAL, true, false, true, 32},

    {AudioBackendType::WM8960, "WM8960", "WM8960 codec",
     "DAC + ADC into an external amplifier.", BackendMaturity::EXPERIMENTAL, true, false, true,
     32},

    {AudioBackendType::TAS5760M, "TAS5760M", "TAS5760M",
     "I2S class-D amplifier, target of the integrated PCB.", BackendMaturity::EXPERIMENTAL, true,
     true, false, 32},
};
}  // namespace

uint8_t audioBackendCount() {
    return static_cast<uint8_t>(sizeof(kBackends) / sizeof(kBackends[0]));
}

const BackendDescriptor& audioBackendDescriptor(uint8_t index) {
    return kBackends[index < audioBackendCount() ? index : 0];
}

const BackendDescriptor* findAudioBackend(AudioBackendType type) {
    for (const auto& d : kBackends) {
        if (d.type == type) return &d;
    }
    return nullptr;
}

IAudioBackend* createAudioBackend(AudioBackendType type) {
    if (!boardCaps().supportsBackend(type)) return nullptr;
    switch (type) {
        case AudioBackendType::ESP32_INTERNAL_DAC:
            return new Esp32DacBackend();
        case AudioBackendType::MAX98357A:
            return new Max98357Backend();
        case AudioBackendType::PCM5102A:
            return new Pcm5102Backend();
        case AudioBackendType::ES8388:
            return new Es8388Backend();
        case AudioBackendType::WM8960:
            return new Wm8960Backend();
        case AudioBackendType::TAS5760M:
            return new Tas5760Backend();
        case AudioBackendType::NONE:
        default:
            return new NullBackend();
    }
}

void destroyAudioBackend(IAudioBackend* backend) {
    if (!backend) return;
    backend->end();
    delete backend;
}

}  // namespace ot
