#include "IECCommands.h"

#include "../../bus/iec/IECHost.h"
#include "../../bus/iec/iec.h"
#include "../Console.h"
#include "../Helpers/PWDHelpers.h"
#include "../../www/ws/activity.h"
#include "../../device/iec/meatloaf.h"
#include "../console_cancel.h"
#include "../dos_encode.h"
#include "../dos_transfer.h"
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

// Offset/hex/ASCII dump. `base` is where these bytes sit in the whole
// transfer, so "read" can dump each chunk as it arrives without accumulating
// the file - the offset column stays continuous across calls.
static void printBytes(const uint8_t *data, size_t size, size_t base)
{
    for (size_t offset = 0; offset < size; offset += 16)
    {
        size_t len = std::min<size_t>(16, size - offset);

        Serial.printf("%04X: ", static_cast<unsigned>(base + offset));
        for (size_t i = 0; i < 16; i++)
        {
            if (i < len)
                Serial.printf("%02X ", data[offset + i]);
            else
                Serial.printf("   ");
        }

        Serial.printf(" ");
        for (size_t i = 0; i < len; i++)
        {
            uint8_t c = data[offset + i];
            Serial.printf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        Serial.printf("\r\n");
    }
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
        Serial.printf("%s\r\n", status.c_str());
    else
        printBytes(reinterpret_cast<const uint8_t *>(status.data()), status.size(), 0);
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

    // No busy check: the console's own "open" leaves channels open, and being
    // able to read the status mid-transfer is most of the point of simulating
    // the C64.  The cost is real and accepted -- if a C64 is mid-transfer on
    // this drive, the console task can step on the IEC task -- and it is the
    // same exposure "mount" and "partition" already carry.

    // Rejoin the tokens the console split on whitespace.
    std::string line;
    for (int i = 1; i < argc; i++)
    {
        if (i > 1) line += ' ';
        line += argv[i];
    }

    std::string command = ESP32Console::encodeDosCommand(line);
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


// ------------------------------------------------ file channels: open/read/
//                                                   write/close
//
// These drive the selected device's FILE channels the way a C64 does, so a
// read or a write inside any mounted media can be exercised without a
// Commodore attached.  The channel number IS the secondary address, exactly as
// in OPEN <lfn>,<dev>,<sa>,"<name>" -- 0 loads, 1 saves, 2-14 are data
// channels and 15 is the command channel.

// Parse a channel argument, rejecting anything a secondary address cannot be.
static bool parseChannel(const char *arg, uint8_t *out)
{
    char *end = nullptr;
    long value = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || value < 0 || value > 15)
        return false;

    *out = (uint8_t) value;
    return true;
}

// Rejoin the tokens the console split on whitespace.  Runs of whitespace
// collapse to one space, because that is all the splitter leaves behind -- put
// the text in quotes when the exact spacing matters.
static std::string joinArgs(int argc, char **argv, int from)
{
    std::string line;
    for (int i = from; i < argc; i++)
    {
        if (i > from) line += ' ';
        line += argv[i];
    }
    return line;
}

// Print the status the drive is left holding, and report whether it is an error.
static int reportStatus(iecDrive *drive)
{
    bool isError = false;
    printStatus(drive->consoleStatus(&isError));
    return isError ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int openChannel(int argc, char **argv)
{
    iecDrive *drive = selectedDrive();
    if (drive == nullptr)
    {
        Serial.printf("No device selected. Run \"use {device id}\" first.\r\n");
        return EXIT_FAILURE;
    }

    uint8_t channel = 0;
    if (argc < 3 || !parseChannel(argv[1], &channel))
    {
        Serial.printf("Usage: open {channel 0-15} {filename}\r\n");
        Serial.printf("       channel is the secondary address: 0 load, 1 save,\r\n");
        Serial.printf("       2-14 data, 15 command.  Type lowercase (PETSCII).\r\n");
        Serial.printf("       e.g. open 0 $            open 2 \"data,s,r\"\r\n");
        Serial.printf("            open 1 @:notes.seq  open 15 i0:\r\n");
        return EXIT_FAILURE;
    }

    std::string name = ESP32Console::encodeDosCommand(joinArgs(argc, argv, 2));

    // Opening 15 with a name RUNS that command on a real C64 -- that is what
    // OPEN 15,8,15,"I0" does -- so route it where "exec" goes rather than
    // handing a command string to the file-open path.
    if (channel == 15)
    {
        bool isError = false;
        printStatus(drive->consoleExecDos(name, &isError));
        return isError ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    drive->consoleOpen(channel, name);

    // The open's own return is not the answer: a lazy stream reports failure
    // through the status channel, which is what the C64 reads too.
    return reportStatus(drive);
}

static int readChannel(int argc, char **argv)
{
    iecDrive *drive = selectedDrive();
    if (drive == nullptr)
    {
        Serial.printf("No device selected. Run \"use {device id}\" first.\r\n");
        return EXIT_FAILURE;
    }

    uint8_t channel = 0;
    if (argc < 2 || !parseChannel(argv[1], &channel))
    {
        Serial.printf("Usage: read {channel 0-15} [byte count]\r\n");
        Serial.printf("       reads to end of file when no count is given.\r\n");
        Serial.printf("       channel 15 reads the status, as INPUT#15 does.\r\n");
        Serial.printf("       press ESC to cancel.\r\n");
        return EXIT_FAILURE;
    }

    // Channel 15 never reaches iecDrive::read() on the bus - IECFileDevice
    // answers it from getStatusData(). That is what INPUT#15,A$ reads, so it
    // must answer here too rather than "61, FILE NOT OPEN".  Printed once:
    // falling through to reportStatus() would consume a second status.
    if (channel == 15)
    {
        bool isError = false;
        printStatus(drive->consoleStatus(&isError));
        return isError ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    size_t limit = 0;   // 0 = to end of file
    if (argc > 2)
    {
        char *end = nullptr;
        long value = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || value < 0)
        {
            Serial.printf("Invalid byte count: %s\r\n", argv[2]);
            return EXIT_FAILURE;
        }
        limit = (size_t) value;
    }

    ESP32Console::cancel_begin();

    bool cancelled = false;
    size_t total = ESP32Console::dos_read_loop(
        limit,
        [drive, channel](uint8_t *buf, uint8_t len) {
            return drive->consoleRead(channel, buf, len);
        },
        [](const uint8_t *buf, size_t len, size_t offset) {
            printBytes(buf, len, offset);
        },
        [] { return ESP32Console::cancel_requested(); },
        &cancelled);

    if (cancelled)
        Serial.printf("cancelled after %u bytes\r\n", (unsigned) total);
    else
        Serial.printf("%u bytes\r\n", (unsigned) total);

    // A read of 0 on the FIRST call is the device's error signal, not an empty
    // file, and the reason is on the status channel (62, FILE NOT FOUND).
    return reportStatus(drive);
}

static int writeChannel(int argc, char **argv)
{
    iecDrive *drive = selectedDrive();
    if (drive == nullptr)
    {
        Serial.printf("No device selected. Run \"use {device id}\" first.\r\n");
        return EXIT_FAILURE;
    }

    uint8_t channel = 0;
    if (argc < 3 || !parseChannel(argv[1], &channel))
    {
        Serial.printf("Usage: write {channel 0-15} {data}\r\n");
        Serial.printf("       type lowercase (PETSCII); binary bytes are written 0xNN,\r\n");
        Serial.printf("       and one 0x may carry a run: 0x000009 == 0x000x000x09\r\n");
        Serial.printf("       runs of whitespace collapse to one space; quote to keep them\r\n");
        Serial.printf("       channel 15 sends a DOS command, as PRINT#15 does.\r\n");
        return EXIT_FAILURE;
    }

    std::string data = ESP32Console::encodeDosCommand(joinArgs(argc, argv, 2));

    // Channel 15 never reaches iecDrive::write() on the bus either - a write
    // there IS a DOS command (IFD_EXEC is set only when m_channel == 15), so
    // PRINT#15,"I0" and "exec i0:" are the same thing.
    if (channel == 15)
    {
        if (data.size() > 255)
            data.resize(255);

        bool isError = false;
        printStatus(drive->consoleExecDos(data, &isError));
        return isError ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    ESP32Console::cancel_begin();

    bool cancelled = false;
    size_t total = ESP32Console::dos_write_loop(
        reinterpret_cast<const uint8_t *>(data.data()), data.size(),
        [drive, channel](const uint8_t *buf, uint8_t len, bool eoi) {
            return drive->consoleWrite(channel, buf, len, eoi);
        },
        [] { return ESP32Console::cancel_requested(); },
        &cancelled);

    if (cancelled)
        Serial.printf("cancelled after %u of %u bytes\r\n",
                      (unsigned) total, (unsigned) data.size());
    else if (total < data.size())
        Serial.printf("%u of %u bytes accepted\r\n",
                      (unsigned) total, (unsigned) data.size());
    else
        Serial.printf("%u bytes\r\n", (unsigned) total);

    return reportStatus(drive);
}

static int listChannels(int argc, char **argv)
{
    iecDrive *drive = selectedDrive();
    if (drive == nullptr)
    {
        Serial.printf("No device selected. Run \"use {device id}\" first.\r\n");
        return EXIT_FAILURE;
    }

    std::vector<iecDrive::ChannelInfo> open_channels = drive->consoleChannels();
    unsigned reported = drive->getNumOpenChannels();

    if (open_channels.empty())
    {
        // A non-zero count with no rows means VDrive owns the channels; say so
        // rather than claiming the drive is idle.
        if (reported > 0)
            Serial.printf("%u channel(s) open, not listable (VDrive owns them).\r\n", reported);
        else
            Serial.printf("No open channels on device #%u.\r\n", (unsigned) drive->id());
        return EXIT_SUCCESS;
    }

    Serial.printf("CH        SIZE         POS  NAME\r\n");
    for (const auto &info : open_channels)
    {
        if (info.has_stream)
            Serial.printf("%2u  %10u  %10u  %s\r\n",
                          (unsigned) info.channel, (unsigned) info.size,
                          (unsigned) info.position, info.name.c_str());
        else
            // A directory listing is generated, not read: no stream, so
            // neither figure exists. Printing 0 would read as an empty file.
            Serial.printf("%2u  %10s  %10s  %s\r\n",
                          (unsigned) info.channel, "-", "-", info.name.c_str());
    }

    if (reported != open_channels.size())
        Serial.printf("(drive reports %u open, listed %u)\r\n",
                      reported, (unsigned) open_channels.size());

    return EXIT_SUCCESS;
}

static int closeChannel(int argc, char **argv)
{
    iecDrive *drive = selectedDrive();
    if (drive == nullptr)
    {
        Serial.printf("No device selected. Run \"use {device id}\" first.\r\n");
        return EXIT_FAILURE;
    }

    uint8_t before = drive->getNumOpenChannels();

    if (argc < 2)
    {
        // Sweep every channel: close() is a no-op on one that is not open, so
        // this needs no per-channel query.
        for (uint8_t channel = 0; channel <= 15; channel++)
            drive->consoleClose(channel);
    }
    else
    {
        uint8_t channel = 0;
        if (!parseChannel(argv[1], &channel))
        {
            Serial.printf("Usage: close [channel 0-15]   (no channel closes all)\r\n");
            return EXIT_FAILURE;
        }
        drive->consoleClose(channel);
    }

    Serial.printf("%u channel(s) closed, %u open\r\n",
                  (unsigned)(before - drive->getNumOpenChannels()),
                  (unsigned) drive->getNumOpenChannels());

    // A write error surfaces HERE - iecChannelHandlerFile's destructor maps
    // the stream's error onto the drive status when the channel is closed.
    return reportStatus(drive);
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
    const ConsoleCommand getOpenCommand()
    {
        return ConsoleCommand("open", &openChannel,
            "Open a file on a channel of the selected device. Usage: open {channel 0-15} {filename}");
    }
    const ConsoleCommand getReadCommand()
    {
        return ConsoleCommand("read", &readChannel,
            "Read a channel of the selected device to end of file (ESC cancels). Usage: read {channel} [byte count]");
    }
    const ConsoleCommand getWriteCommand()
    {
        return ConsoleCommand("write", &writeChannel,
            "Write data to a channel of the selected device. Usage: write {channel} {data}");
    }
    const ConsoleCommand getChannelsCommand()
    {
        return ConsoleCommand("channels", &listChannels,
            "List the open channels on the selected device (number, size, position, name). Usage: channels");
    }
    const ConsoleCommand getCloseCommand()
    {
        return ConsoleCommand("close", &closeChannel,
            "Close a channel of the selected device, or all channels. Usage: close [channel]");
    }
}
