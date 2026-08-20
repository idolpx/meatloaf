#include "IECCommands.h"

#include "../../bus/iec/IECHost.h"
#include "../../bus/iec/iec.h"
#include "../Console.h"
#include "../Helpers/PWDHelpers.h"
#include "../../www/ws/activity.h"
#include "../../device/iec/meatloaf.h"
#include "string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

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
            notify_activity("device" + std::to_string(devnr), "disabled", "conflicting physical device detected");
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
            notify_activity("device" + std::to_string(devnr), "disabled");
            Serial.printf("Device #%u disabled until reset/reboot.\r\n", devnr);
            return EXIT_SUCCESS;
        }
        IEC.end();
        notify_activity("bus", "disabled");
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
            notify_activity("device" + std::to_string(devnr), "active");
            Serial.printf("Device #%u enabled.\r\n", devnr);
            return EXIT_SUCCESS;
        }
        IEC.begin();
        notify_activity("bus", "active");
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

// ------------------------------------------------------------------------
// "use" / "exec" -- drive the currently selected device from the console.
// ------------------------------------------------------------------------

// Device id selected with "use", 0 when none.
static uint8_t s_use_device = 0;

// The drive devices live in the Meatloaf container, not in the bus's device
// table, so resolve through it rather than IEC.findDevice() -- that returns
// an IECDevice*, which cannot be narrowed to iecDrive* without RTTI.
static iecDrive *findDrive(uint8_t devnr)
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        fujiDisk *disk = Meatloaf.get_disks(i);
        if (disk != nullptr && disk->disk_dev.id() == devnr)
            return &disk->disk_dev;
    }

    // Meatloaf itself (device 30) is a drive too, but is not in _fnDisks.
    iecDrive *ml = static_cast<iecDrive *>(&Meatloaf);
    if (ml->id() == devnr)
        return ml;

    return nullptr;
}

// Resolve the selection, clearing it if the device has since gone away.
static iecDrive *selectedDrive()
{
    if (s_use_device == 0)
        return nullptr;

    iecDrive *drive = findDrive(s_use_device);
    if (drive == nullptr)
        s_use_device = 0;

    return drive;
}

static int use(int argc, char **argv)
{
    if (argc < 2)
    {
        iecDrive *drive = selectedDrive();
        if (drive == nullptr)
            Serial.printf("No device selected.\r\n");
        else
            Serial.printf("Using device #%u [%s]\r\n", s_use_device, drive->getCWD().c_str());
        return EXIT_SUCCESS;
    }

    if (!mstr::isNumeric(argv[1]))
    {
        Serial.printf("Usage: use {device id}   (0 = none)\r\n");
        return EXIT_FAILURE;
    }

    int value = atoi(argv[1]);
    if (value < 0 || value > 30)
    {
        Serial.printf("Invalid device ID. Must be 0-30.\r\n");
        return EXIT_FAILURE;
    }

    if (value == 0)
    {
        s_use_device = 0;
        Serial.printf("No device selected.\r\n");
        return EXIT_SUCCESS;
    }

    iecDrive *drive = findDrive(static_cast<uint8_t>(value));
    if (drive == nullptr)
    {
        Serial.printf("No drive device #%d attached.\r\n", value);
        return EXIT_FAILURE;
    }

    s_use_device = static_cast<uint8_t>(value);

    // Take the device to where the console already is; setCurrentPath() keeps
    // it there from now on.
    drive->consoleSetCwd(ESP32Console::getCurrentPathUrl());
    Serial.printf("Using device #%d [%s]\r\n", value, drive->getCWD().c_str());

    return EXIT_SUCCESS;
}

// Encode a console-typed DOS command the way the C64 would put it on the wire.
//
// Text is PETSCII encoded and nothing else.  mstr::toPETSCII2() maps LOWERCASE
// ASCII onto $41-$5A, which is exactly what an unshifted C64 sends and what
// executeData() dispatches on ("CD", "N0", "T-Z") -- so TYPE THE COMMAND IN
// LOWERCASE, as you would at the READY prompt, and the translation makes it
// the right case on the wire.
//
// Nothing is case-folded here, and that is deliberate: commands and their
// parameters can be mixed case, and lowercasing the line to make the VERB
// match would corrupt every filename and path in it.  One encoding, no hidden
// transforms.
//
// Uppercase input is therefore not equivalent -- it maps to the SHIFTED range
// (measured: "M-R" -> CD 2D D2, "I0:" -> C9 30 3A), valid PETSCII that matches
// no command.  It is not a silent trap: executeData() falls through to
// ST_SYNTAX_INVALID, so "exec M-R" answers "31,INVALID COMMAND".
//
// Binary bytes -- M-R/M-W/B-P carry addresses and data that are not text at
// all -- are written as "0x" followed by an even number of hex digits, and
// pass through verbatim, unconverted.  ONE "0x" introduces a RUN of bytes:
// "0x000009" is three bytes 00 00 09, not one byte followed by the text
// "0009".  The per-byte form still works, so "0x000x030x01" is the same three
// bytes as "0x000301" -- the run stops at the 'x', which is not a hex digit.
//
// The ambiguity this accepts, deliberately: a hex escape immediately followed
// by text whose first two characters are hex digits will swallow them
// ("0x00cd" is two bytes 00 CD, not one byte then "cd").  That is tolerable
// because every command carrying binary -- M-R, M-W, M-E, B-P -- is all
// binary after the verb.  Put the text before the escape, or split the run
// with a space, when it matters.
//
// A trailing odd hex digit is left as text rather than guessed at.
static std::string encodeDosCommand(const std::string &line)
{
    std::string out;
    std::string text;   // pending text, converted on the next flush

    auto flushText = [&out, &text]()
    {
        if (text.empty())
            return;
        out += mstr::toPETSCII2(text);
        text.clear();
    };

    // Rejoin the tokens the console split on whitespace.
    for (size_t i = 0; i < line.size(); )
    {
        // Hex bytes are written verbatim -- one "0x" introduces a run of them.
        if (i + 4 <= line.size() && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X') &&
            isxdigit(static_cast<unsigned char>(line[i + 2])) &&
            isxdigit(static_cast<unsigned char>(line[i + 3])))
        {
            flushText();
            size_t j = i + 2;
            while (j + 2 <= line.size() &&
                   isxdigit(static_cast<unsigned char>(line[j])) &&
                   isxdigit(static_cast<unsigned char>(line[j + 1])))
            {
                char hex[3] = { line[j], line[j + 1], '\0' };
                out += static_cast<char>(strtol(hex, nullptr, 16));
                j += 2;
            }
            i = j;
        }
        else
        {
            text += line[i++];
        }
    }
    flushText();

    return out;
}

// Render a status line. M-R and friends answer with raw drive memory, so
// anything that is not plain printable text is dumped as hex.
static void printStatus(const std::string &status)
{
    bool printable = true;
    for (unsigned char c : status)
    {
        if (c < 0x20 || c > 0x7E)
        {
            printable = false;
            break;
        }
    }

    if (printable)
    {
        Serial.printf("%s\r\n", status.c_str());
        return;
    }

    for (size_t offset = 0; offset < status.size(); offset += 16)
    {
        size_t len = std::min<size_t>(16, status.size() - offset);

        Serial.printf("%04X: ", static_cast<unsigned>(offset));
        for (size_t i = 0; i < 16; i++)
        {
            if (i < len)
                Serial.printf("%02X ", static_cast<unsigned char>(status[offset + i]));
            else
                Serial.printf("   ");
        }

        Serial.printf(" ");
        for (size_t i = 0; i < len; i++)
        {
            unsigned char c = static_cast<unsigned char>(status[offset + i]);
            Serial.printf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        Serial.printf("\r\n");
    }
}

static int execDos(int argc, char **argv)
{
    iecDrive *drive = selectedDrive();
    if (drive == nullptr)
    {
        Serial.printf("No device selected. Run \"use {device id}\" first.\r\n");
        return EXIT_FAILURE;
    }

    if (argc < 2)
    {
        // Spacing is sent exactly as typed: "B-P 2 0" wants its separators,
        // "M-R" wants its address bytes butted straight up against the verb.
        Serial.printf("Usage: exec {DOS command}\r\n");
        Serial.printf("       type in lowercase (PETSCII); binary bytes are written 0xNN,\r\n");
        Serial.printf("       and one 0x may carry a run: 0x000009 == 0x000x000x09\r\n");
        Serial.printf("       e.g. exec i0:            exec \"s0:filename\"\r\n");
        Serial.printf("            exec m-r0x000009                 read 9 bytes at $0000\r\n");
        Serial.printf("            exec m-w0x000009010203040506070809  write 9 bytes at $0000\r\n");
        return EXIT_FAILURE;
    }

    // The C64 owns the drive while it has channels open; stepping in from the
    // console task would race the IEC bus task over the same drive state.
    if (drive->getNumOpenChannels() > 0)
    {
        Serial.printf("Device #%u is busy (%u open channel(s)).\r\n",
                      s_use_device, drive->getNumOpenChannels());
        return EXIT_FAILURE;
    }

    // Rejoin the tokens the console split on whitespace.
    std::string line;
    for (int i = 1; i < argc; i++)
    {
        if (i > 1) line += ' ';
        line += argv[i];
    }

    std::string command = encodeDosCommand(line);
    if (command.size() > 255)
        command.resize(255);

    bool isError = false;
    std::string status = drive->consoleExecDos(command, &isError);
    printStatus(status);

    return isError ? EXIT_FAILURE : EXIT_SUCCESS;
}

namespace ESP32Console
{
    int iecSelectedDeviceId()
    {
        return s_use_device;
    }

    void iecSyncSelectedDeviceCwd(const std::string &url)
    {
        iecDrive *drive = selectedDrive();
        if (drive != nullptr)
            drive->consoleSetCwd(url);
    }
}

int enable(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("enable {id_1}|{id_1},{id_2},...\r\n");
        return EXIT_SUCCESS;
    }

    Meatloaf.enable(argv[1]);

    return EXIT_SUCCESS;
}

int disable(int argc, char **argv)
{
    if (argc != 2)
    {
        Serial.printf("disable {id_1}|{id_1},{id_2},...\r\n");
        return EXIT_SUCCESS;
    }

    Meatloaf.disable(argv[1]);

    return EXIT_SUCCESS;
}


namespace ESP32Console::Commands
{
    const ConsoleCommand getIECCommand()
    {
        return ConsoleCommand("iec", &iec,
            "Show/control the IEC bus. Usage: iec [sleep|wake|scan [start] [end]]");
    }
    const ConsoleCommand getEnableCommand()
    {
        return ConsoleCommand("enable", &enable, "Enable virtual device");
    }
    const ConsoleCommand getDisableCommand()
    {
        return ConsoleCommand("disable", &disable, "Disable virtual device");
    }
    const ConsoleCommand getUseCommand()
    {
        return ConsoleCommand("use", &use,
            "Select the device the console drives. Usage: use [device id]  (0 = none)");
    }
    const ConsoleCommand getExecCommand()
    {
        return ConsoleCommand("exec", &execDos,
            "Send a DOS command to the selected device (type lowercase; write binary bytes as 0xNN, one 0x may carry a run). Usage: exec {DOS command}");
    }
}
