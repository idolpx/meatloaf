// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef MEATLOAF_H
#define MEATLOAF_H

#include "drive.h"
#include "fuji.h"

class iecMeatloaf : public iecDrive, public iecFuji
{
private:

protected:

public:
    iecMeatloaf(uint8_t id) : iecDrive(id) {};

// Default VDRIVE state
#ifdef USE_VDRIVE
    bool use_vdrive = true;
#else
    bool use_vdrive = false;
#endif

    void setup(systemBus *bus) override {
        iecFuji::setup(bus);

        if (bus->attachDevice(this))
            Debug_printf("Attached Meatloaf device #%d\r\n", id());
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
        enable_device_basic(deviceids);
    }
    void disable(std::string deviceids) {
        disable_device_basic(deviceids);
    }
};

extern iecMeatloaf Meatloaf;

#endif // MEATLOAF_H