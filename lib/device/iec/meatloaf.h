#ifndef MEATLOAF_H
#define MEATLOAF_H

#include "fuji.h"

class iecMeatloaf : public iecFuji
{
private:

protected:

public:
    iecMeatloaf() {};

    void execute(std::string command) {
        payload = command;
        process_basic_commands();
    }
    void persist_wifi(std::string ssid, std::string password){
        net_store_ssid(ssid, password);
    }

    void enable(std::string deviceids) {
        payload = deviceids;
        enable_device_basic();
    }
    void disable(std::string deviceids) {
        payload = deviceids;
        disable_device_basic();
    }

    bool mount(int deviceid, std::string url);
};

extern iecMeatloaf Meatloaf;

#endif // MEATLOAF_H