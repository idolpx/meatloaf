#include "iec_host.h"

#ifdef BUILD_IEC

#include <algorithm>

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "../../../include/esp-idf-arduino.h"
#include <esp_timer.h>
#include <driver/gpio.h>
#endif

#include "../../../include/cbm_defines.h"
#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

#ifndef PIN_IEC_CLK_IN
#define PIN_IEC_CLK_IN PIN_IEC_CLK_OUT
#endif

#ifndef PIN_IEC_DATA_IN
#define PIN_IEC_DATA_IN PIN_IEC_DATA_OUT
#endif

static constexpr uint8_t IEC_CMD_LISTEN = 0x20;
static constexpr uint8_t IEC_CMD_TALK = 0x40;
static constexpr uint8_t IEC_CMD_UNLISTEN = 0x3F;
static constexpr uint8_t IEC_CMD_UNTALK = 0x5F;
static constexpr uint8_t IEC_SECONDARY_STATUS = 0x6F; // channel 15

iecHost IECHOST;

iecHost::iecHost(systemBus &bus)
    : m_bus(bus)
{
}

void iecHost::setActive(bool active)
{
    if (active == m_active)
        return;

    if (active)
    {
        m_restoreAtnInterrupt = m_bus.isATNInterruptEnabled();
        m_bus.setATNInterruptEnabled(false);
        m_bus.setHostMode(true);
        releaseBusLines();
        m_active = true;
        return;
    }

    releaseBusLines();
    m_bus.setHostMode(false);
    if (m_restoreAtnInterrupt)
        m_bus.setATNInterruptEnabled(true);

    m_restoreAtnInterrupt = false;
    m_active = false;
}

bool iecHost::isLineReleased(uint8_t pin) const
{
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    bool state = gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
#else
    bool state = digitalRead(pin) != 0;
#endif
#if defined(IEC_USE_LINE_DRIVERS) && defined(IEC_USE_INVERTED_INPUTS)
    state = !state;
#endif
    return state;
}

bool iecHost::waitLineReleased(uint8_t pin, uint32_t timeoutUs)
{
    uint64_t start = nowMicros();
    while (!isLineReleased(pin))
    {
        if (timeoutUs > 0 && (nowMicros() - start) >= timeoutUs)
            return false;
    }

    return true;
}

bool iecHost::waitLineAsserted(uint8_t pin, uint32_t timeoutUs)
{
    uint64_t start = nowMicros();
    while (isLineReleased(pin))
    {
        if (timeoutUs > 0 && (nowMicros() - start) >= timeoutUs)
            return false;
    }

    return true;
}

void iecHost::assertATN()
{
    pinMode(PIN_IEC_ATN, OUTPUT);
    digitalWrite(PIN_IEC_ATN, LOW);
}

void iecHost::releaseATN()
{
    pinMode(PIN_IEC_ATN, INPUT);
}

void iecHost::assertCLK()
{
#if defined(IEC_USE_LINE_DRIVERS)
    pinMode(PIN_IEC_CLK_OUT, OUTPUT);
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
    digitalWrite(PIN_IEC_CLK_OUT, HIGH);
#else
    digitalWrite(PIN_IEC_CLK_OUT, LOW);
#endif
#else
    pinMode(PIN_IEC_CLK_OUT, OUTPUT);
    digitalWrite(PIN_IEC_CLK_OUT, LOW);
#endif
}

void iecHost::releaseCLK()
{
#if defined(IEC_USE_LINE_DRIVERS)
    pinMode(PIN_IEC_CLK_OUT, OUTPUT);
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
    digitalWrite(PIN_IEC_CLK_OUT, LOW);
#else
    digitalWrite(PIN_IEC_CLK_OUT, HIGH);
#endif
#else
    pinMode(PIN_IEC_CLK_OUT, INPUT);
#endif
}

void iecHost::assertDATA()
{
#if defined(IEC_USE_LINE_DRIVERS)
    pinMode(PIN_IEC_DATA_OUT, OUTPUT);
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
    digitalWrite(PIN_IEC_DATA_OUT, HIGH);
#else
    digitalWrite(PIN_IEC_DATA_OUT, LOW);
#endif
#else
    pinMode(PIN_IEC_DATA_OUT, OUTPUT);
    digitalWrite(PIN_IEC_DATA_OUT, LOW);
#endif
}

void iecHost::releaseDATA()
{
#if defined(IEC_USE_LINE_DRIVERS)
    pinMode(PIN_IEC_DATA_OUT, OUTPUT);
#ifdef IEC_USE_INVERTED_LINE_DRIVERS
    digitalWrite(PIN_IEC_DATA_OUT, LOW);
#else
    digitalWrite(PIN_IEC_DATA_OUT, HIGH);
#endif
#else
    pinMode(PIN_IEC_DATA_OUT, INPUT);
#endif
}

void iecHost::releaseBusLines()
{
    releaseATN();
    releaseCLK();
    releaseDATA();
}

uint64_t iecHost::nowMicros() const
{
#if defined(ESP_PLATFORM)
    return esp_timer_get_time();
#else
    return micros();
#endif
}

bool iecHost::beginAtnSequence()
{
    assertATN();
    assertCLK();
    releaseDATA();
    delayMicroseconds(5);

    return waitLineAsserted(PIN_IEC_DATA_IN, TIMEOUT_Tat);
}

bool iecHost::endAtnSequence()
{
    releaseATN();
    releaseCLK();
    delayMicroseconds(TIMING_Tr);
    return true;
}

bool iecHost::sendByte(uint8_t value, bool eoi)
{
    releaseCLK();

    if (!waitLineReleased(PIN_IEC_DATA_IN, TIMEOUT_Tf))
        return false;

    if (eoi)
    {
        if (!waitLineAsserted(PIN_IEC_DATA_IN, TIMING_Tye))
            return false;

        if (!waitLineReleased(PIN_IEC_DATA_IN, TIMEOUT_Tne))
            return false;
    }

    assertCLK();
    delayMicroseconds(TIMING_Tpr);

    for (int bit = 0; bit < 8; bit++)
    {
        if ((value & 0x01) != 0)
            releaseDATA();
        else
            assertDATA();

        delayMicroseconds(TIMING_Ts);
        releaseCLK();
        delayMicroseconds(TIMING_Tv64);
        releaseDATA();
        assertCLK();

        value >>= 1;
    }

    if (!waitLineAsserted(PIN_IEC_DATA_IN, TIMEOUT_Tf))
        return false;

    if (eoi)
        releaseCLK();

    return true;
}

bool iecHost::readByte(uint8_t &value, bool &eoi)
{
    value = 0;
    eoi = false;

    releaseDATA();

    if (!waitLineReleased(PIN_IEC_CLK_IN, TIMEOUT_Tf))
        return false;

    if (!waitLineAsserted(PIN_IEC_CLK_IN, TIMING_Tye))
    {
        assertDATA();
        delayMicroseconds(TIMING_Tei);
        releaseDATA();

        if (!waitLineAsserted(PIN_IEC_CLK_IN, TIMEOUT_Tne))
            return false;

        eoi = true;
    }

    for (int bit = 0; bit < 8; bit++)
    {
        if (!waitLineReleased(PIN_IEC_CLK_IN, TIMEOUT_Tf))
            return false;

        if (isLineReleased(PIN_IEC_DATA_IN))
            value |= uint8_t(1u << bit);

        if (!waitLineAsserted(PIN_IEC_CLK_IN, TIMEOUT_Tf))
            return false;
    }

    assertDATA();

    return true;
}

bool iecHost::deviceExists(uint8_t deviceID)
{
    bool startedHere = false;
    if (!m_active)
    {
        setActive(true);
        startedHere = true;
    }

    bool exists = false;

    if (beginAtnSequence())
    {
        exists = sendByte(IEC_CMD_LISTEN | (deviceID & 0x1F));
        endAtnSequence();

        if (beginAtnSequence())
        {
            sendByte(IEC_CMD_UNLISTEN);
            endAtnSequence();
        }
    }

    if (startedHere)
        setActive(false);

    return exists;
}

bool iecHost::sendCommand(uint8_t deviceID, const std::string &command)
{
    if (!beginAtnSequence())
        return false;

    if (!sendByte(IEC_CMD_LISTEN | (deviceID & 0x1F)))
    {
        endAtnSequence();
        return false;
    }

    if (!sendByte(IEC_SECONDARY_STATUS))
    {
        endAtnSequence();
        return false;
    }

    endAtnSequence();

    for (size_t i = 0; i < command.size(); i++)
    {
        if (!sendByte(uint8_t(command[i]), i == (command.size() - 1)))
            return false;
    }

    if (!beginAtnSequence())
        return false;

    bool ok = sendByte(IEC_CMD_UNLISTEN);
    endAtnSequence();

    return ok;
}

bool iecHost::initializeDevice(uint8_t deviceID)
{
    bool startedHere = false;
    if (!m_active)
    {
        setActive(true);
        startedHere = true;
    }

    bool ok = sendCommand(deviceID, "I0");

    if (startedHere)
        setActive(false);

    return ok;
}

std::string iecHost::getStatus(uint8_t deviceID, uint16_t maxBytes)
{
    bool startedHere = false;
    if (!m_active)
    {
        setActive(true);
        startedHere = true;
    }

    std::string status;

    if (!beginAtnSequence())
    {
        if (startedHere)
            setActive(false);
        return status;
    }

    if (!sendByte(IEC_CMD_TALK | (deviceID & 0x1F)) || !sendByte(IEC_SECONDARY_STATUS))
    {
        endAtnSequence();
        if (startedHere)
            setActive(false);
        return status;
    }

    endAtnSequence();

    // role-reversal entry state expected by IEC talkers
    releaseCLK();
    assertDATA();
    delayMicroseconds(TIMING_Ttk);

    bool eoi = false;
    for (uint16_t i = 0; i < maxBytes; i++)
    {
        uint8_t value = 0;
        if (!readByte(value, eoi))
            break;

        status.push_back(static_cast<char>(value));

        if (eoi)
            break;
    }

    if (beginAtnSequence())
    {
        sendByte(IEC_CMD_UNTALK);
        endAtnSequence();
    }

    releaseDATA();

    if (startedHere)
        setActive(false);

    return status;
}

const std::map<uint8_t, IECDiscoveredDevice> &iecHost::discoverDevices(uint8_t firstDeviceID, uint8_t lastDeviceID)
{
    m_devices.clear();

    firstDeviceID = std::max<uint8_t>(firstDeviceID, 4);
    lastDeviceID = std::min<uint8_t>(lastDeviceID, 30);
    if (firstDeviceID > lastDeviceID)
        return m_devices;

    bool startedHere = false;
    if (!m_active)
    {
        setActive(true);
        startedHere = true;
    }

    for (uint8_t deviceID = firstDeviceID; deviceID <= lastDeviceID; deviceID++)
    {
        if (!deviceExists(deviceID))
            continue;

        initializeDevice(deviceID);
        std::string status = getStatus(deviceID);

        IECDiscoveredDevice info;
        info.deviceID = deviceID;
        info.status = status;
        m_devices[deviceID] = info;

        Debug_printf("IEC host discovered device %u status[%s]\r\n", deviceID, status.c_str());
    }

    if (startedHere)
        setActive(false);

    return m_devices;
}

#else

iecHost IECHOST;

iecHost::iecHost(systemBus &bus)
    : m_bus(bus)
{
}

void iecHost::setActive(bool active)
{
    m_active = active;
}

bool iecHost::deviceExists(uint8_t)
{
    return false;
}

bool iecHost::initializeDevice(uint8_t)
{
    return false;
}

std::string iecHost::getStatus(uint8_t, uint16_t)
{
    return "";
}

const std::map<uint8_t, IECDiscoveredDevice> &iecHost::discoverDevices(uint8_t, uint8_t)
{
    m_devices.clear();
    return m_devices;
}

#endif // BUILD_IEC
