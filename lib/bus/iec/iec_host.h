#ifndef MEATLOAF_BUS_IECHOST
#define MEATLOAF_BUS_IECHOST

#include <cstdint>
#include <map>
#include <string>

#include "iec.h"

struct IECDiscoveredDevice
{
    uint8_t deviceID = 0;
    std::string status;
};

class iecHost
{
public:
    explicit iecHost(systemBus &bus = IEC);

    void setActive(bool active);
    bool isActive() const { return m_active; }

    bool deviceExists(uint8_t deviceID);
    bool initializeDevice(uint8_t deviceID);
    std::string getStatus(uint8_t deviceID, uint16_t maxBytes = 96);

    const std::map<uint8_t, IECDiscoveredDevice> &discoverDevices(uint8_t firstDeviceID = 4, uint8_t lastDeviceID = 30);
    const std::map<uint8_t, IECDiscoveredDevice> &getDiscoveredDevices() const { return m_devices; }

private:
    systemBus &m_bus;
    bool m_active = false;
    bool m_restoreAtnInterrupt = false;

    std::map<uint8_t, IECDiscoveredDevice> m_devices;

    bool sendCommand(uint8_t deviceID, const std::string &command);
    bool beginAtnSequence();
    bool endAtnSequence();
    bool sendByte(uint8_t value, bool eoi = false);
    bool readByte(uint8_t &value, bool &eoi);

    bool waitLineReleased(uint8_t pin, uint32_t timeoutUs);
    bool waitLineAsserted(uint8_t pin, uint32_t timeoutUs);
    bool isLineReleased(uint8_t pin) const;

    void assertATN();
    void releaseATN();
    void assertCLK();
    void releaseCLK();
    void assertDATA();
    void releaseDATA();
    void releaseBusLines();

    uint64_t nowMicros() const;
};

extern iecHost IECHOST;

#endif // MEATLOAF_BUS_IECHOST
