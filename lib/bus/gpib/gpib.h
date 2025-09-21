//
// https://www.pagetable.com/?p=1023
// https://ia601902.us.archive.org/9/items/PET_and_the_GPIB_Bus_1980_McGraw-Hill/PET_and_the_GPIB_Bus_1980_McGraw-Hill.pdf
// https://www.youtube.com/watch?v=EEtETGfL_VE
//

#ifndef GPIB_H
#define GPIB_H

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

#include "GPIBDevice.h"
#include "GPIBBusHandler.h"

#define BUS_DEVICEID_GLOBAL     0   // Addresses all devices
#define BUS_DEVICEID_PRINTER    4   // 4-7
#define BUS_DEVICEID_DISK       8   // 8-15
#define BUS_DEVICEID_NETWORK    16  // 16-19
#define BUS_DEVICEID_OTHER      20  // 20-29
#define BUS_DEVICEID_SYSTEM     30


/**
 * @brief The command frame
 */
// union cmdFrame_t
// {
//     struct
//     {
//         uint8_t device;
//         uint8_t comnd;
//         uint8_t aux1;
//         uint8_t aux2;
//         uint8_t cksum;
//     };
//     struct
//     {
//         uint32_t commanddata;
//         uint8_t checksum;
//     } __attribute__((packed));
// };


/**
 * @class systemBus
 * @brief the system bus that all virtualDevices attach to.
 */
class ieee488Bus : public GPIBBusHandler
{
public:
    ieee488Bus();

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
extern ieee488Bus GPIB;

#endif /* GPIB_H */
