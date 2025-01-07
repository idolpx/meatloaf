#ifndef MEATLOAF_H
#define MEATLOAF_H

#include "fuji.h"

class iecMeatloaf : public iecFuji
{
private:

protected:

public:
    iecMeatloaf() {};

    void persist_wifi(std::string ssid, std::string password){
        net_store_ssid(ssid, password);
    }
};

extern iecMeatloaf Meatloaf;

#endif // MEATLOAF_H