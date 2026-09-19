// Host stub for the WiFi manager.
//
// modem.cpp asks it two questions: whether there is a carrier to dial over
// (doDial's NO DIALTONE guard) and the MAC address for ATI. The suite drives
// executeLine() directly and never dials, so both answers are fixed; the
// connected flag is public so a test can set NO DIALTONE deliberately.
#ifndef ML_STUB_FNWIFI_H
#define ML_STUB_FNWIFI_H

#include <string>

class MlStubWiFi
{
public:
    bool connected() { return connected_; }
    std::string get_mac_str() { return "00:00:00:00:00:00"; }

    bool connected_ = true;
};

extern MlStubWiFi fnWiFi;

#endif
