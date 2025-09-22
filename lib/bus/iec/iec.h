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

#ifndef IEC_H
#define IEC_H

#include <cstdint>
#include <forward_list>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <utility>
#include <string>
#include <map>
#include <queue>
#include <memory>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "fnSystem.h"

#include <soc/gpio_reg.h>

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

#include "IECDevice.h"
#include "IECBusHandler.h"

#define BUS_DEVICEID_GLOBAL     0   // Addresses all devices
#define BUS_DEVICEID_PRINTER    4   // 4-7
#define BUS_DEVICEID_DISK       8   // 8-15
#define BUS_DEVICEID_NETWORK    16  // 16-19
#define BUS_DEVICEID_OTHER      20  // 20-29
#define BUS_DEVICEID_SYSTEM     30


/**
 * @brief The command frame
 */
union cmdFrame_t
{
    struct
    {
        uint8_t device;
        uint8_t comnd;
        uint8_t aux1;
        uint8_t aux2;
        uint8_t cksum;
    };
    struct
    {
        uint32_t commanddata;
        uint8_t checksum;
    } __attribute__((packed));
};


/**
 * @class systemBus
 * @brief the system bus that all virtualDevices attach to.
 */
class systemBus : public IECBusHandler
{
public:
    systemBus();

    /**
     * @brief called in main.cpp to set up the bus.
     */
    void setup();

    /**
     * @brief Run one iteration of the bus service loop
     */
    void service();

    /**
     * @brief called from main shutdown to clean up the device.
     */
    void shutdown();

    /**
     * @brief Are we shutting down?
     * @return value of shuttingDown
     */
    bool getShuttingDown() { return shuttingDown; }

 private:
    /**
     * @brief is device shutting down?
     */
    bool shuttingDown = false;

};
/**
 * @brief Return
 */
extern systemBus IEC;

#endif /* IEC_H */
