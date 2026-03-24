// -----------------------------------------------------------------------------
// IECHost - Commodore IEC serial bus master implementation for Meatloaf
//
// Allows the ESP32 to act as a bus master (like a Commodore 64/128) and
// communicate with IEC peripheral devices (disk drives, printers, etc.).
//
// Supported transfer protocols (auto-detected per device):
//   Standard IEC, JiffyDOS
//   SpeedDOS / DolphinDOS — detected; parallel-cable transfer not implemented
//   Epyx / FC3 / AR6       — detected; fallback to standard IEC
//
// Usage:
//   IECHost host(IEC);          // IEC is the systemBus (IECBusHandler)
//   host.scanBus(8, 15);        // detect drives, negotiates protocol
//   host.sendCommand(8, "I0");  // initialize drive 8
//   host.readStatus(8, buf, sizeof(buf));
// -----------------------------------------------------------------------------

#ifndef IECHOST_H
#define IECHOST_H

#include "IECBusHandler.h"
#include "IECConfig.h"

#include <stdint.h>
#include <string>
#include <map>

// IEC bus command bytes (sent under ATN by host)
#define IEC_CMD_LISTEN      0x20  // 0x20|devnr  — tell device to listen
#define IEC_CMD_UNLISTEN    0x3F  // broadcast   — release all listeners
#define IEC_CMD_TALK        0x40  // 0x40|devnr  — tell device to talk
#define IEC_CMD_UNTALK      0x5F  // broadcast   — release all talkers
#define IEC_CMD_OPEN        0xF0  // 0xF0|ch     — open channel (secondary address, after LISTEN)
#define IEC_CMD_CLOSE       0xE0  // 0xE0|ch     — close channel (secondary address, after LISTEN)
#define IEC_CMD_REOPEN      0x60  // 0x60|ch     — reopen/data channel (secondary address)

// IEC device address ranges
#define IEC_DEV_PRINTER_MIN  4
#define IEC_DEV_DISK_MIN     8
#define IEC_DEV_DISK_MAX    15
#define IEC_DEV_MAX         30

// IEC status / command channel
#define IEC_CHANNEL_CMD     15   // command / status channel
#define IEC_CHANNEL_LOAD     0   // default load channel
#define IEC_CHANNEL_SAVE     1   // default save channel

// Negotiated fast-transfer protocol for a device.
// Values mirror IEC_FP_* so they can be compared directly.
#define IEC_HOST_PROT_NONE      0xFF  // no fast protocol; use standard IEC
#define IEC_HOST_PROT_JIFFY     IEC_FP_JIFFY      // 0 — JiffyDOS
#define IEC_HOST_PROT_EPYX      IEC_FP_EPYX       // 1 — Epyx FastLoad
#define IEC_HOST_PROT_FC3       IEC_FP_FC3        // 2 — Final Cartridge 3
#define IEC_HOST_PROT_AR6       IEC_FP_AR6        // 3 — Action Replay 6
#define IEC_HOST_PROT_DOLPHIN   IEC_FP_DOLPHIN    // 4 — DolphinDOS (parallel)
#define IEC_HOST_PROT_SPEEDDOS  IEC_FP_SPEEDDOS   // 5 — SpeedDOS (parallel)


// Information about a detected IEC device
struct IECDeviceInfo {
    bool     present;           // device responded to probe
    bool     initialized;       // initialization command was sent
    uint8_t  protocol;          // negotiated transfer protocol (IEC_HOST_PROT_*)
    char     status[256];       // last status string from channel 15
    uint8_t  statusCode;        // numeric part of status (e.g. 00, 73, ...)
};


class IECHost
{
public:
    // bus: reference to the IECBusHandler (typically the global IEC / systemBus)
    explicit IECHost(IECBusHandler& bus);

    // -------------------------------------------------------------------------
    // Bus scanning & device management
    // -------------------------------------------------------------------------

    // Probe a range of device IDs.  Detected devices are added to m_devices.
    // Returns the number of devices found.
    // Defaults to the full disk-drive range (8-15).
    int scanBus(uint8_t fromDevnr = IEC_DEV_DISK_MIN,
                uint8_t toDevnr  = IEC_DEV_DISK_MAX);

    // Probe a single device and negotiate its transfer protocol.
    // Returns true if the device is present.
    bool probeDevice(uint8_t devnr);

    // Send the "initialize" command ("I0") to a device.
    // If devnr==0 all known devices are initialized.
    bool initDevice(uint8_t devnr);

    // Get the map of all detected devices (keyed by device number).
    const std::map<uint8_t, IECDeviceInfo>& getDevices() const { return m_devices; }

    // -------------------------------------------------------------------------
    // High-level device I/O
    // -------------------------------------------------------------------------

    // Send a CBM-DOS command string to channel 15 of the given device.
    // cmd can be anything accepted by the drive's command channel:
    //   "I0"  — initialize
    //   "S:filename" — scratch file
    //   "N:diskname,id" — format
    //   etc.
    bool sendCommand(uint8_t devnr, const char* cmd, uint8_t len = 0);

    // Read the status string from channel 15 of the given device.
    // Returns number of bytes placed in buf (NUL-terminated).
    uint8_t readStatus(uint8_t devnr, char* buf, uint8_t bufSize);

    // Open a channel on a device and write data to it.
    // channel: 2-14 for sequential files, 1 for save
    // filename: passed as the secondary OPEN payload
    bool openForWrite(uint8_t devnr, uint8_t channel, const char* filename);

    // Write data to an already-open channel.
    bool writeData(uint8_t devnr, uint8_t channel,
                   const uint8_t* data, uint8_t len);

    // Open a channel on a device for reading.
    bool openForRead(uint8_t devnr, uint8_t channel, const char* filename);

    // Read data from an already-open channel.
    // Returns number of bytes read, sets eoi=true on last byte.
    uint8_t readData(uint8_t devnr, uint8_t channel,
                     uint8_t* buf, uint8_t bufSize, bool& eoi);

    // Close a channel on a device.
    bool closeChannel(uint8_t devnr, uint8_t channel);

    // Assert or release the hardware RESET line (if wired).
    void resetBus();

private:
    // -------------------------------------------------------------------------
    // Host mode entry / exit (manages ATN interrupt and m_hostMode flag)
    // -------------------------------------------------------------------------
    void enterHostMode();
    void exitHostMode();

    // -------------------------------------------------------------------------
    // ATN line control
    // IECBusHandler has no writePinATN — we drive it directly and keep
    // m_bus.m_flags in sync so its waitPin* functions remain consistent.
    // -------------------------------------------------------------------------
    void hAssertATN();
    void hReleaseATN();

    // -------------------------------------------------------------------------
    // Standard IEC byte transfer (host as sender / receiver)
    // -------------------------------------------------------------------------

    // Send one byte under the current bus state.
    // probeJiffy: if true, extend CLK-LOW on bit 7 to probe for JiffyDOS support.
    // jiffyAck:   set to true if the device acknowledged the JiffyDOS probe.
    bool hSendByte(uint8_t data, bool eoi,
                   bool probeJiffy = false, bool* jiffyAck = nullptr);

    // Receive one byte from a talking device.
    bool hRecvByte(uint8_t& data, bool& eoi);

    // -------------------------------------------------------------------------
    // JiffyDOS C64-side byte transfer
    // These mirror transmitJiffyByte / receiveJiffyByte in IECBusHandler but
    // implement the HOST (C64) perspective.
    // -------------------------------------------------------------------------
    bool hJiffyRecvByte(uint8_t& data, bool& eoi);   // host receives from talking device
    bool hJiffySendByte(uint8_t data, bool eoi);      // host sends to listening device

    // -------------------------------------------------------------------------
    // ATN sequence helpers
    // -------------------------------------------------------------------------

    // Assert ATN, wait for device ack, send cmdLen bytes, leave ATN asserted.
    // Caller must call hReleaseATN() then exitHostMode() when done.
    // Returns false if no device responds within 1 ms.
    bool hBeginATN();

    // Full open: LISTEN|devnr, OPEN|ch, [filename], UNLISTEN
    bool cmdOpen(uint8_t devnr, uint8_t channel,
                 const char* filename, uint8_t nameLen);

    // Close: LISTEN|devnr, CLOSE|ch, UNLISTEN  (all under one ATN assertion)
    bool cmdClose(uint8_t devnr, uint8_t channel);

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    IECBusHandler&                   m_bus;
    std::map<uint8_t, IECDeviceInfo> m_devices;
    bool                             m_wasATNEnabled;  // saved interrupt state
};

#endif // IECHOST_H
