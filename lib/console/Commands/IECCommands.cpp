#include "IECCommands.h"

#include "../../bus/iec/IECHost.h"
#include "../../bus/iec/iec.h"
#include "../Console.h"

#include <cstdlib>
#include <cstring>

#ifdef BUILD_IEC
static const char *deviceTypeLabel(uint8_t devnr)
{
    if (devnr < BUS_DEVICEID_PRINTER) return "system";
    if (devnr < BUS_DEVICEID_DRIVE)    return "printer";
    if (devnr < BUS_DEVICEID_NETWORK) return "drive";
    if (devnr < BUS_DEVICEID_OTHER)   return "network";
    if (devnr < BUS_DEVICEID_SYSTEM)  return "other";
    return "meatloaf";
}

static void iecStatus()
{
    Serial.printf("IEC bus: %s\r\n", IEC.isEnabled() ? "enabled" : "disabled");

    if (IEC.m_numDevices == 0)
    {
        Serial.printf("No devices attached.\r\n");
        return;
    }

    Serial.printf("Attached devices:\r\n");
    for (uint8_t i = 0; i < IEC.m_numDevices; i++)
    {
        IECDevice *dev = IEC.m_devices[i];
        Serial.printf(" #%-2d: %-8s  %s\r\n",
                      dev->getDeviceNumber(),
                      deviceTypeLabel(dev->getDeviceNumber()),
                      dev->isActive() ? "active" : "inactive");
    }
}

static int iecScan(int argc, char **argv)
{
    uint8_t first = BUS_DEVICEID_PRINTER;
    uint8_t last = BUS_DEVICEID_SYSTEM;

    if (argc >= 3)
    {
        int value = atoi(argv[2]);
        if (value < 0 || value > 30)
        {
            Serial.printf("Invalid start device ID. Must be 0-30.\r\n");
            return EXIT_FAILURE;
        }
        first = static_cast<uint8_t>(value);
    }

    if (argc >= 4)
    {
        int value = atoi(argv[3]);
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

    IECHost host(IEC);
    int found = host.scanBus(first, last);

    if (found < 0)
    {
        Serial.printf("IEC bus not connected (RESET line is low) -- aborting scan.\r\n");
        return EXIT_FAILURE;
    }

    if (found == 0)
    {
        Serial.printf("No physical IEC devices discovered.\r\n");
        return EXIT_SUCCESS;
    }

    // scanBus() pre-populates an entry for every ID in the scanned range, whether
    // or not anything responded, so only entries with present==true are real.
    Serial.printf("Discovered %d physical IEC device(s):\r\n", found);
    for (const auto &entry : host.getDevices())
    {
        if (!entry.second.present)
            continue;

        uint8_t devnr = entry.first;
        const auto &device = entry.second;
        Serial.printf(" #%-2d:  %s\r\n", devnr, device.status);

        // Only disable a virtual device if a physical device with the same ID
        // actually responded on the wire.
        IECDevice *virt = IEC.findDevice(devnr);
        if (virt != nullptr)
        {
            virt->setActive(false);
            Serial.printf(" #%-2d: disabled conflicting virtual %s device\r\n", devnr, deviceTypeLabel(devnr));
        }
    }

    return EXIT_SUCCESS;
}

static int iec(int argc, char **argv)
{
    if (argc < 2)
    {
        iecStatus();
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "sleep") == 0)
    {
        if (argc >= 3)
        {
            int value = atoi(argv[2]);
            if (value < 0 || value > 30)
            {
                Serial.printf("Invalid device ID. Must be 0-30.\r\n");
                return EXIT_FAILURE;
            }
            uint8_t devnr = static_cast<uint8_t>(value);
            IECDevice *dev = IEC.findDevice(devnr, true);
            if (dev == nullptr)
            {
                Serial.printf("No device #%u attached.\r\n", devnr);
                return EXIT_FAILURE;
            }
            dev->setActive(false);
            Serial.printf("Device #%u disabled until reset/reboot.\r\n", devnr);
            return EXIT_SUCCESS;
        }
        IEC.end();
        Serial.printf("IEC bus disabled.\r\n");
        return EXIT_SUCCESS;
    }
    else if (strcmp(argv[1], "wake") == 0)
    {
        if (argc >= 3)
        {
            int value = atoi(argv[2]);
            if (value < 0 || value > 30)
            {
                Serial.printf("Invalid device ID. Must be 0-30.\r\n");
                return EXIT_FAILURE;
            }
            uint8_t devnr = static_cast<uint8_t>(value);
            IECDevice *dev = IEC.findDevice(devnr, true);
            if (dev == nullptr)
            {
                Serial.printf("No device #%u attached.\r\n", devnr);
                return EXIT_FAILURE;
            }
            dev->setActive(true);
            Serial.printf("Device #%u enabled.\r\n", devnr);
            return EXIT_SUCCESS;
        }
        IEC.begin();
        Serial.printf("IEC bus enabled.\r\n");
        return EXIT_SUCCESS;
    }
    else if (strcmp(argv[1], "scan") == 0)
    {
        return iecScan(argc, argv);
    }

    Serial.printf("Usage: iec [sleep|wake [id]|scan [start] [end]]\r\n");
    return EXIT_FAILURE;
}
#else
static int iec(int argc, char **argv)
{
    Serial.printf("IEC bus support is disabled in this build.\r\n");
    return EXIT_FAILURE;
}
#endif

namespace ESP32Console::Commands
{
    const ConsoleCommand getIECCommand()
    {
        return ConsoleCommand("iec", &iec,
            "Show/control the IEC bus. Usage: iec [sleep|wake|scan [start] [end]]");
    }
}
