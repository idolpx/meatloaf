#ifndef MEATLOAF_H
#define MEATLOAF_H

#include "drive.h"
#include "fuji.h"

class iecMeatloaf : public iecDrive, public iecFuji
{
private:

protected:

public:
    //iecMeatloaf() {};

    void setup(systemBus *bus) override {
        iecFuji::setup(bus);

        setDeviceNumber(30); 
        if (bus->attachDevice(this))
            Debug_printf("Attached Meatloaf device #%d\r\n", 30);
    }

    void execute(const char *command, uint8_t cmdLen) override {
        iecDrive::execute(command, cmdLen);

        payload = command;
        iecFuji::process_cmd();
    }

    void execute(std::string command) {
        payload = command;
        process_basic_commands();
    }

    void enable(std::string deviceids) {
        payload = deviceids;
        enable_device_basic();
    }
    void disable(std::string deviceids) {
        payload = deviceids;
        disable_device_basic();
    }
};

extern iecMeatloaf Meatloaf;

#endif // MEATLOAF_H