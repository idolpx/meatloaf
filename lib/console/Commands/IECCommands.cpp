#include "IECCommands.h"

#include "../../bus/iec/iec_host.h"

static int iecdetect(int argc, char **argv)
{
#ifndef BUILD_IEC
    Serial.printf("IEC bus support is disabled in this build.\r\n");
    return EXIT_FAILURE;
#else
    uint8_t first = BUS_DEVICEID_PRINTER;
    uint8_t last = BUS_DEVICEID_SYSTEM;

    if (argc >= 2)
    {
        int value = atoi(argv[1]);
        if (value < 0 || value > 30)
        {
            Serial.printf("Invalid start device ID. Must be 0-30.\r\n");
            return EXIT_FAILURE;
        }
        first = static_cast<uint8_t>(value);
    }

    if (argc >= 3)
    {
        int value = atoi(argv[2]);
        if (value < 0 || value > 30)
        {
            Serial.printf("Invalid end device ID. Must be 0-30.\r\n");
            return EXIT_FAILURE;
        }
        last = static_cast<uint8_t>(value);
    }

    if (first > last)
    {
        Serial.printf("Invalid range: start device ID must be <= end device ID.\r\n");
        return EXIT_FAILURE;
    }

    Serial.printf("Scanning IEC devices in range %u-%u ...\r\n", first, last);

    const auto &devices = IECHOST.discoverDevices(first, last);
    if (devices.empty())
    {
        Serial.printf("No IEC devices discovered.\r\n");
        return EXIT_SUCCESS;
    }

    Serial.printf("Discovered %u IEC device(s):\r\n", static_cast<unsigned>(devices.size()));
    for (const auto &entry : devices)
    {
        const auto &device = entry.second;
        Serial.printf("  #%u  %s\r\n", device.deviceID, device.status.c_str());
    }

    return EXIT_SUCCESS;
#endif
}

namespace ESP32Console::Commands
{
    const ConsoleCommand getIECDetectCommand()
    {
        return ConsoleCommand("iecdetect", &iecdetect, "Detect IEC devices and print status (usage: iecdetect [start] [end])");
    }
}
