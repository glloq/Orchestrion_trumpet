#include "audio/IAudioBackend.h"

namespace ot {

void IAudioBackend::writeSamples32(const int32_t* buffer, size_t count) {
    // Default behaviour for a 16 bit only backend: narrow in fixed size chunks
    // so nothing is allocated on the audio path.
    if (!buffer || count == 0) return;
    int16_t tmp[128];
    size_t offset = 0;
    while (offset < count) {
        const size_t chunk = (count - offset) > 128 ? 128 : (count - offset);
        for (size_t i = 0; i < chunk; ++i) tmp[i] = static_cast<int16_t>(buffer[offset + i] >> 16);
        writeSamples(tmp, chunk);
        offset += chunk;
    }
}

}  // namespace ot
