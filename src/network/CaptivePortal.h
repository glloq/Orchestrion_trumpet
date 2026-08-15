// ============================================================================
//  CaptivePortal.h - "tap the notification to configure".
//
//  While the hotspot is up, every DNS query is answered with 192.168.4.1 so a
//  phone opens the configuration page by itself.  Disabled as soon as the
//  instrument is on a real network, and switchable from the web UI.
// ============================================================================
#pragma once

#include "config/ConfigTypes.h"

#if !defined(OT_HOST_BUILD)
#include <DNSServer.h>
#endif

namespace ot {

class CaptivePortal {
public:
    bool begin();
    void end();
    void loop();
    bool active() const { return active_; }

private:
#if !defined(OT_HOST_BUILD)
    DNSServer dns_;
#endif
    bool active_ = false;
};

}  // namespace ot
