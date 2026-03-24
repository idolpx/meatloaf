// -----------------------------------------------------------------------------
// IECHost.cpp — Commodore IEC serial bus master for Meatloaf
//
// Uses IECBusHandler's private pin/wait/timer methods (via friend access) so
// all bus-line timing goes through the same code paths as device mode.
// ATN interrupt is disabled for the duration of every host operation.
// -----------------------------------------------------------------------------

#include "IECHost.h"

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "../../../include/esp-idf-arduino.h"
#endif

#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// P_ATN flag value — matches the file-local define in IECBusHandler.cpp.
// We keep m_bus.m_flags in sync so IECBusHandler::waitPinCLK/DATA correctly
// avoid treating our own ATN assertion as a spurious ATN change.
// ---------------------------------------------------------------------------
static constexpr uint8_t HOST_P_ATN = 0x80;

// ---------------------------------------------------------------------------
// Cycle-counted timer for JiffyDOS sampling (needs ~1 µs precision).
// Mirrors the timer_* macros in IECBusHandler.cpp.
// ---------------------------------------------------------------------------
#if defined(ESP_PLATFORM)

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_clk.h"
#define esp_cpu_cycle_count_t uint32_t
#define esp_cpu_get_cycle_count  esp_cpu_get_ccount
#define esp_rom_get_cpu_ticks_per_us() (esp_clk_cpu_freq() / 1000000)
#endif

static DRAM_ATTR uint32_t h_timer_start;
static DRAM_ATTR uint32_t h_cycles_per_us_div2;

#define H_TIMER_INIT()      h_cycles_per_us_div2 = esp_rom_get_cpu_ticks_per_us() / 2
#define H_TIMER_RESET()     h_timer_start = esp_cpu_get_cycle_count()
#define H_WAIT_UNTIL(us)  { uint32_t _to = (uint32_t)((us)*2) * h_cycles_per_us_div2; \
                             while ((esp_cpu_get_cycle_count() - h_timer_start) < _to); }

#else  // non-ESP32 fallback

static unsigned long h_timer_start;
#define H_TIMER_INIT()
#define H_TIMER_RESET()    h_timer_start = micros()
#define H_WAIT_UNTIL(us)   while ((micros() - h_timer_start) < (unsigned long)(us))

#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

IECHost::IECHost(IECBusHandler& bus)
    : m_bus(bus), m_wasATNEnabled(false)
{
    H_TIMER_INIT();
}

// ---------------------------------------------------------------------------
// Host mode entry / exit
// ---------------------------------------------------------------------------

void IECHost::enterHostMode()
{
    m_wasATNEnabled = m_bus.isATNInterruptEnabled();
    m_bus.setATNInterruptEnabled(false);
    m_bus.setHostMode(true);
}

void IECHost::exitHostMode()
{
    m_bus.setHostMode(false);
    m_bus.setATNInterruptEnabled(m_wasATNEnabled);
}

// ---------------------------------------------------------------------------
// ATN line control
//
// IECBusHandler treats ATN as INPUT (it's a C64 output in device mode).
// In host mode we drive it: OUTPUT+LOW = asserted, INPUT = released.
// We also update m_flags so IECBusHandler's waitPin* ATN-change checks work.
// ---------------------------------------------------------------------------

void IECHost::hAssertATN()
{
    digitalWrite(m_bus.m_pinATN, LOW);
    pinMode(m_bus.m_pinATN, OUTPUT);
    m_bus.m_flags |= HOST_P_ATN;
    delayMicroseconds(10);
}

void IECHost::hReleaseATN()
{
    pinMode(m_bus.m_pinATN, INPUT);
    m_bus.m_flags &= ~HOST_P_ATN;
    delayMicroseconds(20);   // Tbb: ≥20 µs before first CLK transition
}

// ---------------------------------------------------------------------------
// hBeginATN — assert ATN and wait for any device to acknowledge (DATA LOW).
// Returns false if nothing responds within 1 ms.
// ATN remains asserted on return; caller must hReleaseATN() when done.
// ---------------------------------------------------------------------------

bool IECHost::hBeginATN()
{
    hAssertATN();
    m_bus.writePinCLK(false);   // CLK LOW = "not ready to send"
    m_bus.writePinDATA(true);   // release DATA

    // Devices pull DATA LOW within 1 ms to acknowledge ATN
    if (!m_bus.waitPinDATA(false, 1000)) {
        hReleaseATN();
        m_bus.writePinCLK(true);
        m_bus.writePinDATA(true);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// hSendByte — host sends one byte (host drives CLK).
//
// Standard IEC send protocol:
//   1. CLK HIGH = "ready to send"  (device pulled DATA LOW after ATN or last byte)
//   2. If eoi: hold CLK HIGH >200 µs; wait for device to ack (DATA LOW→HIGH)
//   3. Wait for device DATA LOW = "ready for data"
//   4. 8 bits LSB-first: CLK LOW | set DATA | wait 80 µs | CLK HIGH | wait 70 µs
//   5. CLK LOW, release DATA
//   6. Wait for device DATA LOW = accepted (timeout 1 ms)
//
// probeJiffy: on the 8th bit (bit 7) hold CLK LOW for 250 µs instead of 80 µs.
//   A JiffyDOS-capable device responds by pulsing DATA LOW within that window.
//   If jiffyAck is non-null, *jiffyAck is set accordingly.
// ---------------------------------------------------------------------------

bool IECHost::hSendByte(uint8_t data, bool eoi,
                        bool probeJiffy, bool* jiffyAck)
{
    if (jiffyAck) *jiffyAck = false;

    // Step 1: release CLK = "ready to send"
    m_bus.writePinCLK(true);

    if (eoi) {
        // Hold CLK HIGH >200 µs to signal End-Of-Indicator
        if (!m_bus.waitTimeout(250)) return false;
        // Device acknowledges EOI by DATA LOW then DATA HIGH
        if (!m_bus.waitPinDATA(false, 1000)) return false;
        if (!m_bus.waitPinDATA(true,  1000)) return false;
    }

    // Step 3: device pulls DATA LOW ("ready for data")
    if (!m_bus.waitPinDATA(false, 1000)) return false;

    // Step 4: send 8 bits, LSB first
    for (uint8_t i = 0; i < 8; i++) {
        m_bus.writePinCLK(false);              // CLK LOW = data not valid
        m_bus.writePinDATA((data & 1) != 0);   // set bit
        data >>= 1;

        if (probeJiffy && i == 7) {
            // JiffyDOS probe: hold CLK LOW for >200 µs so devices can detect us
            if (!m_bus.waitTimeout(250)) return false;

            // Check if device acknowledged JiffyDOS (DATA LOW pulse)
            if (jiffyAck && !m_bus.readPinDATA())
                *jiffyAck = true;
        } else {
            if (!m_bus.waitTimeout(80)) return false;   // Tpr = 80 µs
        }

        m_bus.writePinCLK(true);                   // CLK HIGH = data valid
        if (!m_bus.waitTimeout(70)) return false;  // Tpa = 70 µs
    }

    // Step 5: CLK LOW, release DATA
    m_bus.writePinCLK(false);
    m_bus.writePinDATA(true);

    // Step 6: device pulls DATA LOW to acknowledge receipt
    if (!m_bus.waitPinDATA(false, 1000)) return false;

    return true;
}

// ---------------------------------------------------------------------------
// hRecvByte — host receives one byte from a talking device (device drives CLK).
//
// Standard IEC receive:
//   1. Release DATA = "ready for data"
//   2. Wait for device to release CLK HIGH = "ready to send" (200 µs timeout)
//      If no CLK HIGH in 200 µs → device is signaling EOI
//      Acknowledge EOI: DATA LOW ~80 µs then HIGH
//      Then wait for CLK HIGH
//   3. 8 bits LSB-first: wait CLK HIGH, read DATA, wait CLK LOW
//   4. Pull DATA LOW = "data accepted"
// ---------------------------------------------------------------------------

bool IECHost::hRecvByte(uint8_t& data, bool& eoi)
{
    eoi = false;

    // Step 1: release DATA = ready for data
    m_bus.writePinDATA(true);

    // Step 2: wait for CLK HIGH, 200 µs timeout
    if (!m_bus.waitPinCLK(true, 200)) {
        // Timeout → EOI signaled
        eoi = true;
        m_bus.writePinDATA(false);
        if (!m_bus.waitTimeout(80)) return false;
        m_bus.writePinDATA(true);

        // Now wait for CLK HIGH (no short timeout — device will send as soon as ready)
        if (!m_bus.waitPinCLK(true, 5000)) return false;
    }

    // Step 3: receive 8 bits, LSB first
    data = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (!m_bus.waitPinCLK(true,  1000)) return false;  // wait CLK HIGH = valid
        data >>= 1;
        if (m_bus.readPinDATA()) data |= 0x80;
        if (!m_bus.waitPinCLK(false, 1000)) return false;  // wait CLK LOW = done
    }

    // Step 4: acknowledge
    m_bus.writePinDATA(false);
    return true;
}

// ---------------------------------------------------------------------------
// hJiffyRecvByte — JiffyDOS C64-side receive (host receives from device).
//
// Mirrors the HOST perspective of IECBusHandler::transmitJiffyByte:
//   Device releases CLK HIGH = "ready to send"
//   Host releases DATA HIGH  = "ready to receive" → starts device timer
//   Device outputs 2 bits simultaneously on CLK+DATA at precise intervals
//   Host samples at ~16, 27, 39, 50 µs from DATA HIGH
//   At ~61 µs: CLK HIGH = EOI, DATA HIGH = error
//   Host pulls DATA LOW to acknowledge
// ---------------------------------------------------------------------------

bool IRAM_ATTR IECHost::hJiffyRecvByte(uint8_t& data, bool& eoi)
{
    eoi = false;
    data = 0;

    H_TIMER_INIT();

    portDISABLE_INTERRUPTS();

    // Wait for device to release CLK HIGH ("ready to send")
    // Device holds CLK LOW until it has data ready.
    // Use a generous timeout (5 ms) in case the device is slow.
    {
        uint32_t t0 = micros();
        while (!m_bus.readPinCLK()) {
            if (!m_bus.readPinATN()) { portENABLE_INTERRUPTS(); return false; }
            if ((micros() - t0) > 5000) { portENABLE_INTERRUPTS(); return false; }
        }
    }

    // Release DATA HIGH = "ready to receive"
    // This starts the device's timer — from this moment on, timing is critical.
    m_bus.writePinDATA(true);
    H_TIMER_RESET();

    // The device sets bits 0,1 on CLK,DATA immediately after DATA goes HIGH.
    // We sample at ~16 µs.
    H_WAIT_UNTIL(16);
    if (!m_bus.readPinCLK())  data |= (1 << 0);
    if (!m_bus.readPinDATA()) data |= (1 << 1);

    // Bits 2, 3 at ~27 µs
    H_WAIT_UNTIL(27);
    if (!m_bus.readPinCLK())  data |= (1 << 2);
    if (!m_bus.readPinDATA()) data |= (1 << 3);

    // Bits 4, 5 at ~39 µs
    H_WAIT_UNTIL(39);
    if (!m_bus.readPinCLK())  data |= (1 << 4);
    if (!m_bus.readPinDATA()) data |= (1 << 5);

    // Bits 6, 7 at ~50 µs
    H_WAIT_UNTIL(50);
    if (!m_bus.readPinCLK())  data |= (1 << 6);
    if (!m_bus.readPinDATA()) data |= (1 << 7);

    // EOI / more / error status at ~61 µs:
    //   CLK=LOW,  DATA=HIGH → more data follows
    //   CLK=HIGH, DATA=LOW  → EOI (last byte)
    //   CLK=HIGH, DATA=HIGH → error
    H_WAIT_UNTIL(61);
    bool clk_hi = m_bus.readPinCLK();
    bool dat_hi = m_bus.readPinDATA();
    eoi = (clk_hi && !dat_hi);

    // Acknowledge: pull DATA LOW
    m_bus.writePinDATA(false);

    portENABLE_INTERRUPTS();

    // Error: CLK=HIGH, DATA=HIGH
    if (clk_hi && dat_hi) return false;

    return true;
}

// ---------------------------------------------------------------------------
// hJiffySendByte — JiffyDOS C64-side send (host sends to listening device).
//
// Mirrors the HOST perspective of IECBusHandler::receiveJiffyByte:
//   Device releases DATA HIGH = "ready for data"
//   Host releases CLK HIGH    = "ready to send" → starts device timer
//   Host outputs 2 bits simultaneously on CLK+DATA at precise intervals
//   (note: bit ordering differs from receive — this is how the 1541 ROM does it)
//   At ~64 µs: EOI is signaled on CLK (HIGH = EOI)
//   Device acknowledges at ~83 µs by pulling DATA LOW
// ---------------------------------------------------------------------------

bool IRAM_ATTR IECHost::hJiffySendByte(uint8_t data, bool eoi)
{
    H_TIMER_INIT();

    portDISABLE_INTERRUPTS();

    // Wait for device to release DATA HIGH = "ready for data"
    {
        uint32_t t0 = micros();
        while (!m_bus.readPinDATA()) {
            if (!m_bus.readPinATN()) { portENABLE_INTERRUPTS(); return false; }
            if ((micros() - t0) > 5000) { portENABLE_INTERRUPTS(); return false; }
        }
    }

    // Release CLK HIGH = "ready to send"
    // This starts the device's timer.
    m_bus.writePinCLK(true);
    H_TIMER_RESET();

    // Device reads pairs at 14, 27, 38, 51 µs from CLK HIGH.
    // Bit ordering (from 1541 JiffyDOS ROM FC51–FC6B):
    //   bits 4,5 at ~14 µs
    //   bits 6,7 at ~27 µs
    //   bits 3,1 at ~38 µs
    //   bits 2,0 at ~51 µs

    // Bits 4, 5 — set before 14 µs
    m_bus.writePinCLK((data >> 4) & 1);
    m_bus.writePinDATA((data >> 5) & 1);
    H_WAIT_UNTIL(14);

    // Bits 6, 7 — set before 27 µs
    m_bus.writePinCLK((data >> 6) & 1);
    m_bus.writePinDATA((data >> 7) & 1);
    H_WAIT_UNTIL(27);

    // Bits 3, 1 — set before 38 µs
    m_bus.writePinCLK((data >> 3) & 1);
    m_bus.writePinDATA((data >> 1) & 1);
    H_WAIT_UNTIL(38);

    // Bits 2, 0 — set before 51 µs
    m_bus.writePinCLK((data >> 2) & 1);
    m_bus.writePinDATA((data >> 0) & 1);
    H_WAIT_UNTIL(51);

    // EOI signal at ~61 µs: CLK HIGH = EOI, CLK LOW = more data
    m_bus.writePinCLK(eoi);
    m_bus.writePinDATA(true);   // release DATA
    H_WAIT_UNTIL(64);

    portENABLE_INTERRUPTS();

    // Wait for device to acknowledge (DATA LOW)
    if (!m_bus.waitPinDATA(false, 1000)) return false;

    return true;
}

// ---------------------------------------------------------------------------
// probeDevice — detect presence and negotiate fast protocol.
//
// 1. Send TALK | devnr under ATN; probe for JiffyDOS by delaying CLK >200 µs
//    on the last bit of the primary address byte.
// 2. After ATN release, watch for the device to take over CLK (talker turn-around).
// 3. If JiffyDOS was acknowledged, record it; otherwise standard IEC.
// 4. Clean up with UNTALK.
// ---------------------------------------------------------------------------

bool IECHost::probeDevice(uint8_t devnr)
{
    enterHostMode();

    if (!hBeginATN()) {
        exitHostMode();
        return false;
    }

    // Send TALK | devnr — probe for JiffyDOS on this byte
    bool jiffyAck = false;
    bool ok = hSendByte(IEC_CMD_TALK | (devnr & 0x1F), false,
                        /*probeJiffy=*/true, &jiffyAck);

    // Release ATN — talker turn-around
    hReleaseATN();

    bool present = false;
    if (ok) {
        // Host releases CLK; a present device claims the bus by pulling CLK LOW
        m_bus.writePinCLK(true);

        uint32_t t0 = micros();
        while ((micros() - t0) < 1000) {
            if (!m_bus.readPinCLK()) { present = true; break; }
        }
    }

    // UNTALK — assert ATN to abort the talk sequence cleanly
    hAssertATN();
    m_bus.writePinCLK(false);
    m_bus.waitPinDATA(false, 1000);  // re-ack (brief)
    hSendByte(IEC_CMD_UNTALK, false);
    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();

    if (present) {
        uint8_t prot = IEC_HOST_PROT_NONE;
        if (jiffyAck)
            prot = IEC_HOST_PROT_JIFFY;
        // Future: probe SpeedDOS / DolphinDOS via parallel handshake here

        auto it = m_devices.find(devnr);
        if (it != m_devices.end()) {
            it->second.present  = true;
            it->second.protocol = prot;
        }
    }

    return present;
}

// ---------------------------------------------------------------------------
// scanBus
// ---------------------------------------------------------------------------

int IECHost::scanBus(uint8_t fromDevnr, uint8_t toDevnr)
{
    int found = 0;
    for (uint8_t d = fromDevnr; d <= toDevnr; d++) {
        // Pre-populate entry so probeDevice can update protocol field
        IECDeviceInfo info;
        memset(&info, 0, sizeof(info));
        info.present   = false;
        info.protocol  = IEC_HOST_PROT_NONE;
        info.statusCode = 0xFF;
        m_devices[d] = info;

        if (probeDevice(d)) {
            m_devices[d].present = true;
            found++;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// initDevice
// ---------------------------------------------------------------------------

bool IECHost::initDevice(uint8_t devnr)
{
    if (devnr == 0) {
        bool allOk = true;
        for (auto& kv : m_devices)
            if (kv.second.present)
                allOk &= initDevice(kv.first);
        return allOk;
    }

    bool ok = sendCommand(devnr, "I0", 2);
    if (ok) {
        auto it = m_devices.find(devnr);
        if (it != m_devices.end())
            it->second.initialized = true;

        char buf[256];
        uint8_t len = readStatus(devnr, buf, (uint8_t)(sizeof(buf) - 1));
        if (len > 0 && it != m_devices.end()) {
            strncpy(it->second.status, buf, sizeof(it->second.status) - 1);
            it->second.status[sizeof(it->second.status) - 1] = '\0';
            it->second.statusCode = (uint8_t)atoi(buf);
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// sendCommand — send a CBM-DOS command string to channel 15.
//
//   [ATN] LISTEN|devnr, OPEN|15  [/ATN]
//   [data] cmd bytes              [/data]
//   [ATN] UNLISTEN               [/ATN]
// ---------------------------------------------------------------------------

bool IECHost::sendCommand(uint8_t devnr, const char* cmd, uint8_t len)
{
    if (len == 0) len = (uint8_t)strlen(cmd);

    enterHostMode();

    if (!hBeginATN()) { exitHostMode(); return false; }

    bool ok = hSendByte(IEC_CMD_LISTEN | (devnr & 0x1F), false);
    if (ok) ok = hSendByte(IEC_CMD_OPEN | IEC_CHANNEL_CMD, false);

    hReleaseATN();

    for (uint8_t i = 0; i < len && ok; i++)
        ok = hSendByte((uint8_t)cmd[i], (i == len - 1));

    // UNLISTEN
    if (!hBeginATN()) { exitHostMode(); return false; }
    if (ok) hSendByte(IEC_CMD_UNLISTEN, false);
    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();
    return ok;
}

// ---------------------------------------------------------------------------
// readStatus — read status string from channel 15.
// Uses the device's negotiated protocol for the data phase.
// ---------------------------------------------------------------------------

uint8_t IECHost::readStatus(uint8_t devnr, char* buf, uint8_t bufSize)
{
    if (bufSize == 0) return 0;

    enterHostMode();

    if (!hBeginATN()) { exitHostMode(); return 0; }

    bool ok = hSendByte(IEC_CMD_TALK | (devnr & 0x1F), false);
    if (ok) ok = hSendByte(IEC_CMD_REOPEN | IEC_CHANNEL_CMD, false);

    // Turn-around: release ATN, host releases CLK, device takes CLK as talker
    hReleaseATN();

    if (!ok) {
        m_bus.writePinCLK(true);
        m_bus.writePinDATA(true);
        exitHostMode();
        return 0;
    }

    m_bus.writePinCLK(true);  // host releases CLK; device pulls it LOW

    // Wait for device to pull CLK LOW (talker turn-around, ≤80 µs)
    if (!m_bus.waitPinCLK(false, 1000)) {
        m_bus.writePinDATA(true);
        exitHostMode();
        return 0;
    }

    // Determine which receive function to use
    uint8_t prot = IEC_HOST_PROT_NONE;
    {
        auto it = m_devices.find(devnr);
        if (it != m_devices.end())
            prot = it->second.protocol;
    }

    uint8_t count = 0;
    bool eoi = false;

    while (!eoi && count < (uint8_t)(bufSize - 1)) {
        uint8_t byte;
        bool recvOk;

        if (prot == IEC_HOST_PROT_JIFFY)
            recvOk = hJiffyRecvByte(byte, eoi);
        else
            recvOk = hRecvByte(byte, eoi);

        if (!recvOk) break;
        if (byte == 0x0D) break;  // CR = end of status string
        buf[count++] = (char)byte;
    }
    buf[count] = '\0';

    // UNTALK
    hAssertATN();
    m_bus.writePinCLK(false);
    m_bus.waitPinDATA(false, 1000);
    hSendByte(IEC_CMD_UNTALK, false);
    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();
    return count;
}

// ---------------------------------------------------------------------------
// cmdOpen — full OPEN sequence
//   [ATN] LISTEN|devnr, OPEN|ch  [/ATN]  [data] filename  [/data]  [ATN] UNLISTEN [/ATN]
// ---------------------------------------------------------------------------

bool IECHost::cmdOpen(uint8_t devnr, uint8_t channel,
                      const char* filename, uint8_t nameLen)
{
    enterHostMode();

    if (!hBeginATN()) { exitHostMode(); return false; }

    bool ok = hSendByte(IEC_CMD_LISTEN | (devnr & 0x1F), false);
    if (ok) ok = hSendByte(IEC_CMD_OPEN | (channel & 0x0F), false);

    hReleaseATN();

    for (uint8_t i = 0; i < nameLen && ok; i++)
        ok = hSendByte((uint8_t)filename[i], (i == nameLen - 1));

    if (!hBeginATN()) { exitHostMode(); return false; }
    if (ok) hSendByte(IEC_CMD_UNLISTEN, false);
    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();
    return ok;
}

// ---------------------------------------------------------------------------
// cmdClose — LISTEN|devnr, CLOSE|ch, UNLISTEN  (all under one ATN assertion)
// ---------------------------------------------------------------------------

bool IECHost::cmdClose(uint8_t devnr, uint8_t channel)
{
    enterHostMode();

    if (!hBeginATN()) { exitHostMode(); return false; }

    bool ok = hSendByte(IEC_CMD_LISTEN  | (devnr   & 0x1F), false);
    if (ok) ok = hSendByte(IEC_CMD_CLOSE  | (channel & 0x0F), false);
    if (ok) ok = hSendByte(IEC_CMD_UNLISTEN,                   false);

    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();
    return ok;
}

// ---------------------------------------------------------------------------
// Public high-level I/O
// ---------------------------------------------------------------------------

bool IECHost::openForWrite(uint8_t devnr, uint8_t channel, const char* filename)
{
    return cmdOpen(devnr, channel, filename, (uint8_t)strlen(filename));
}

bool IECHost::writeData(uint8_t devnr, uint8_t channel,
                        const uint8_t* data, uint8_t len)
{
    if (len == 0) return true;

    enterHostMode();

    if (!hBeginATN()) { exitHostMode(); return false; }

    bool ok = hSendByte(IEC_CMD_LISTEN | (devnr   & 0x1F), false);
    if (ok) ok = hSendByte(IEC_CMD_REOPEN | (channel & 0x0F), false);

    hReleaseATN();

    // Determine protocol
    uint8_t prot = IEC_HOST_PROT_NONE;
    {
        auto it = m_devices.find(devnr);
        if (it != m_devices.end()) prot = it->second.protocol;
    }

    for (uint8_t i = 0; i < len && ok; i++) {
        bool isLast = (i == len - 1);
        if (prot == IEC_HOST_PROT_JIFFY)
            ok = hJiffySendByte(data[i], isLast);
        else
            ok = hSendByte(data[i], isLast);
    }

    if (!hBeginATN()) { exitHostMode(); return false; }
    if (ok) hSendByte(IEC_CMD_UNLISTEN, false);
    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();
    return ok;
}

bool IECHost::openForRead(uint8_t devnr, uint8_t channel, const char* filename)
{
    return cmdOpen(devnr, channel, filename, (uint8_t)strlen(filename));
}

uint8_t IECHost::readData(uint8_t devnr, uint8_t channel,
                           uint8_t* buf, uint8_t bufSize, bool& eoi)
{
    eoi = false;
    if (bufSize == 0) return 0;

    enterHostMode();

    if (!hBeginATN()) { exitHostMode(); return 0; }

    bool ok = hSendByte(IEC_CMD_TALK   | (devnr   & 0x1F), false);
    if (ok) ok = hSendByte(IEC_CMD_REOPEN | (channel & 0x0F), false);

    hReleaseATN();

    if (!ok) {
        m_bus.writePinCLK(true);
        m_bus.writePinDATA(true);
        exitHostMode();
        return 0;
    }

    m_bus.writePinCLK(true);  // host releases CLK; device pulls LOW

    if (!m_bus.waitPinCLK(false, 1000)) {
        m_bus.writePinDATA(true);
        exitHostMode();
        return 0;
    }

    uint8_t prot = IEC_HOST_PROT_NONE;
    {
        auto it = m_devices.find(devnr);
        if (it != m_devices.end()) prot = it->second.protocol;
    }

    uint8_t count = 0;
    while (!eoi && count < bufSize) {
        uint8_t byte;
        bool recvOk;

        if (prot == IEC_HOST_PROT_JIFFY)
            recvOk = hJiffyRecvByte(byte, eoi);
        else
            recvOk = hRecvByte(byte, eoi);

        if (!recvOk) break;
        buf[count++] = byte;
    }

    // UNTALK
    hAssertATN();
    m_bus.writePinCLK(false);
    m_bus.waitPinDATA(false, 1000);
    hSendByte(IEC_CMD_UNTALK, false);
    hReleaseATN();
    m_bus.writePinCLK(true);
    m_bus.writePinDATA(true);

    exitHostMode();
    return count;
}

bool IECHost::closeChannel(uint8_t devnr, uint8_t channel)
{
    return cmdClose(devnr, channel);
}

// ---------------------------------------------------------------------------
// resetBus — pulse the RESET line
// ---------------------------------------------------------------------------

void IECHost::resetBus()
{
    if (m_bus.m_pinRESET == 0xFF) return;

    digitalWrite(m_bus.m_pinRESET, LOW);
    pinMode(m_bus.m_pinRESET, OUTPUT);
    delayMicroseconds(500);
    pinMode(m_bus.m_pinRESET, INPUT);
    for (int i = 0; i < 500; i++)
        delayMicroseconds(1000);  // 500 ms re-init wait
}
