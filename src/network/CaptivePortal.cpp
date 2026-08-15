#include "network/CaptivePortal.h"

#include "diagnostics/Logger.h"

#if !defined(OT_HOST_BUILD)
#include <WiFi.h>
#endif

namespace ot {

bool CaptivePortal::begin() {
#if defined(OT_HOST_BUILD)
    return false;
#else
    if (active_) return true;
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    active_ = dns_.start(53, "*", WiFi.softAPIP());
    if (active_) OT_LOGI("portal", "captive portal answering on 53/udp");
    return active_;
#endif
}

void CaptivePortal::end() {
#if !defined(OT_HOST_BUILD)
    if (active_) dns_.stop();
#endif
    active_ = false;
}

void CaptivePortal::loop() {
#if !defined(OT_HOST_BUILD)
    if (active_) dns_.processNextRequest();
#endif
}

}  // namespace ot
