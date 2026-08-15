// Deterministic clock for the host build: the tests drive time by hand so a
// solenoid timeout or a servo ramp can be verified without waiting for it.
#include "core/Platform.h"

namespace ot {
namespace {
uint32_t g_millis = 0;
}
uint32_t hostMillis() { return g_millis; }
void hostSetMillis(uint32_t ms) { g_millis = ms; }
}  // namespace ot
