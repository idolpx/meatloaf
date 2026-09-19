// Host stub for the system manager.
//
// modem.cpp reaches exactly one member of it, fnSystem.Net's IPv4 address
// string, which ATI prints.
#ifndef ML_STUB_FNSYSTEM_H
#define ML_STUB_FNSYSTEM_H

#include <string>

class MlStubNet
{
public:
    std::string get_ip4_address_str() { return "0.0.0.0"; }
};

class MlStubSystem
{
public:
    MlStubNet Net;
};

extern MlStubSystem fnSystem;

#endif
