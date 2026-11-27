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

// The command channel, part 2 of 3: the SD2IEC extensions
// (http://www.n2dvm.com/UIEC.pdf) and the CMD HD/FD partition commands. Tried
// after the CBM DOS commands, before the Meatloaf ones.
//
//   cbm_dos.cpp       CBM DOS 2.6 / 1581
//   sd2iec_dos.cpp    this file
//   meatloaf_dos.cpp  Meatloaf's own commands

#if defined(BUILD_IEC)

#include "cbm_dos.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <memory>

#include "../../drive.h"

#include "mlConfig.h"
#include "utils.h"
#include "time_converter.h"

#include "../../../../../include/debug.h"
#include "../../../../../include/cbm_defines.h"


// Only decides WHICH command it is; each has its own function below.
bool CbmDos::cmdSd2iec(std::string& command, size_t colon_position)
{
    switch ( command[0] )
    {
        case 'C':
            if ( command[1] == 'P' || command[1] == 'p' ) // CP<n> / C{Shift-P}<byte>
                return cmdChangePartition(command);
            break;

        case 'E':
            if ( command[1] == '-' )
                Debug_printv("eeprom");                   // E-x: not implemented
            break;

        case 'G':
            // G-P[<partition>]: CMD partition info
            if ( command.size() >= 3 && command[1] == '-' && command[2] == 'P' )
            {
                cmdGetPartitionInfo();
                return true;
            }
            break;

        case 'M':
            if ( command[1] == 'D' )
            {
                cmdMakeDirectory(command.substr(colon_position + 1));
                return true;
            }
            break;

        case 'P':
            if ( isBarePwd(command) )
            {
                cmdPrintWorkingDirectory();
                return true;
            }
            break;

        case 'R':
            if ( command[1] == 'D' )
            {
                cmdRemoveDirectory(command.substr(colon_position + 1));
                return true;
            }
            break;

        case 'S':
            // S-D / S-<n> swap drive number, S-C SCSI command: accepted, not implemented
            if ( command[1] == '-' && command.size() > 2 &&
                 (command[2] == 'D' || command[2] == 'C' || mstr::isNumeric(&command[2])) )
            {
                Debug_printv("S-%c not implemented", command[2]);
                return true;
            }
            break;

        case 'T':
            if ( command[1] == '-' && command.size() > 2 )
            {
                if ( command[2] == 'R' )
                    cmdReadTime(command);
                else if ( command[2] == 'W' && command.size() > 3 && command[3] == 'Z' )
                    cmdSetTimezone(command, colon_position);
                else if ( command[2] == 'W' )
                    cmdSetTime(command, colon_position);
                else
                    m_host.dosSetStatus(ST_SYNTAX_INVALID);
                return true;
            }
            break;

        case 'W':
            // W-0 / W-1 write protect off/on: accepted, not implemented
            if ( command[1] == '-' && (command[2] == '0' || command[2] == '1') )
            {
                Debug_printv("write protect %s", command[2] == '1' ? "enabled" : "disabled");
                return true;
            }
            break;

        case 'X':
            // XR:{name} loads a drive ROM. The other X commands (X{0-4},
            // XE+/-, XB+/-, X{0-7}={0-15}, XD?, XJ+/-, XF+/-, XS, XW, X?)
            // are not implemented.
            if ( command[1] == 'R' && colon_position )
            {
                cmdSetRom(command.substr(colon_position + 1));
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}


// --- MD, RD, PWD ----------------------------------------------------------------

// MD:path. Inside a container a new directory is a fresh image (format), on a
// host filesystem it is mkdir().
void CbmDos::cmdMakeDirectory(const std::string& arg)
{
    std::string path = mstr::toUTF8(arg);
    Debug_printv("make directory path[%s]", path.c_str());
    std::unique_ptr<MFile> f(m_host.dosCwd()->cd(path));
    if ( f == nullptr )
    {
        Debug_printv("make directory failed");
        m_host.dosSetStatus(ST_WRITE_VERIFY);
        return;
    }

    if ( f->sourceFile != nullptr )
    {
        if ( f->sourceFile->exists() )
        {
            Debug_printv("directory exists");
            m_host.dosSetStatus(ST_FILE_EXISTS);
        }
        else if ( !f->sourceFile->isWritable )
        {
            Debug_printv("not writable");
            m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
        }
        else if ( f->format("meatloaf,01") )
        {
            Debug_printv("format ok");
            return;
        }
    }

    if ( f->exists() )
    {
        Debug_printv("directory exists");
        m_host.dosSetStatus(ST_FILE_EXISTS);
    }
    else if ( !f->isWritable )
    {
        Debug_printv("not writable");
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
    }
    else if ( f->mkDir() )
    {
        Debug_printv("mkdir ok");
    }
    else
    {
        Debug_printv("make directory failed");
        m_host.dosSetStatus(ST_WRITE_VERIFY);
    }
}

void CbmDos::cmdRemoveDirectory(const std::string& arg)
{
    std::string path = mstr::toUTF8(arg);
    Debug_printv("remove directory path[%s]", path.c_str());
    std::unique_ptr<MFile> f(m_host.dosCwd()->cd(path));
    if ( f == nullptr )
        return;
    if ( !f->exists() )
        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
    else if ( !f->isWritable )
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
    else if ( !f->rmDir() )
        m_host.dosSetStatus(ST_WRITE_VERIFY);
}

void CbmDos::cmdPrintWorkingDirectory()
{
    Debug_printv("print working directory");
    std::string path = mstr::toPETSCII2(m_host.dosCwd()->url);
    path += "\r";
    m_host.dosSetStatusData((const char *) path.c_str(), path.length());
}


// --- XR -------------------------------------------------------------------------

void CbmDos::cmdSetRom(const std::string& arg)
{
    std::string name = mstr::toUTF8(arg);
    Debug_printv("rom[%s]", name.c_str());
    // It used to break out of the switch here, so the final 31 overwrote the 62.
    if ( !m_host.dosMemory().setROM(name) )
        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
}


// --- T-R, T-W ------------------------------------------------------------------

// T-RA / T-RI / T-RD / T-RB read the clock in each format, T-RZ the timezone.
void CbmDos::cmdReadTime(const std::string& command)
{
    time_t tt = time(nullptr);
    struct tm *tinfo = localtime(&tt);

    uint8_t buf[IECFILEDEVICE_STATUS_BUFFER_SIZE];
    size_t len = 0;
    char form = command.size() > 3 ? command[3] : 0;

    switch ( form )
    {
        case 'A': len = TimeConverter::toAsciiTime(tinfo, buf, sizeof(buf));   break;
        case 'I': len = TimeConverter::toIsoTime(tinfo, buf, sizeof(buf));     break;
        case 'D': len = TimeConverter::toDecimalTime(tinfo, buf, sizeof(buf)); break;
        case 'B': len = TimeConverter::toBcdTime(tinfo, buf, sizeof(buf));     break;
        case 'Z':
        {
            const char *tz_value = getenv("TZ");
            std::string tz = mstr::toPETSCII2(tz_value != nullptr ? tz_value : "UTC");
            Debug_printv("read TIMEZONE [%s]", tz.c_str());
            len = std::min(tz.size(), sizeof(buf));
            memcpy(buf, tz.data(), len);
            break;
        }
        default:
            // vdrive_command_time: anything but A/D/B (and our I/Z)
            m_host.dosSetStatus(ST_SYNTAX_INVALID);
            return;
    }
    m_host.dosSetStatusData((const char *) buf, len);
}

// T-WZ:<IANA name> sets the timezone and keeps it across a reboot.
void CbmDos::cmdSetTimezone(const std::string& command, size_t colon_position)
{
    std::string tz = colon_position ? command.substr(colon_position + 1) : mstr::drop(command, 4);
    tz = mstr::toUTF8(tz);
    mstr::trim(tz);
    mstr::replaceAll(tz, " ", "_");
    Debug_printv("write TIMEZONE [%s]", tz.c_str());

    const char *posix_tz = iana_to_posix_tz(tz);
    setenv("TZ", posix_tz ? posix_tz : tz.c_str(), 1);
    tzset();

    // Persist the IANA name (not the resolved POSIX string) so mlConfig.load()
    // re-resolves it the same way on the next boot.
    json_object_at(mlConfig.data(), "preferences")["timezone"] = tz;
    mlConfig.save();
}

// CP / C{Shift-P} on media with no partition table (anything but a CMD or CFS
// image): "31 INVALID COMMAND", exactly what iecDrive::changePartition() answers
// when hdpart::targetFor() finds no table. CmdHdDos and Ide64Dos override this.
bool CbmDos::cmdChangePartition(const std::string& command)
{
    (void) command;
    m_host.dosSetStatus(ST_SYNTAX_INVALID);
    return true;
}


// CP<n> (text) and C{Shift-P}<byte> (binary), for media that have partitions.
bool CbmDos::partitionFromCommand(const std::string& command)
{
    if ( command[1] == 'P') // Change Partition text: CP<n>
    {
        // Digits must start right after "CP" - find_first_of over the
        // whole string would let "CPX5" parse as partition 5. strtol
        // (not atoi) per the project rule against atoi/std::stoi on
        // C64-sourced input; leave pnum = -1 on any parse failure so
        // hdpart::select() (called via changePartition()) rejects it,
        // range-checking per format.
        int pnum = -1;
        char *end = nullptr;
        const char *start = command.c_str() + 2;
        long v = strtol(start, &end, 10);
        if ( end != start )
        {
            while ( *end == ' ' || *end == '\r' )
                end++;
            if ( *end == '\0' )
                pnum = (int)v;
        }
        m_host.dosChangePartition(pnum);
        return true;
    }
    else // command[1] == 'p': Change Partition binary: C{Shift-P}<byte>
    {
        m_host.dosChangePartition(command.size() > 2 ? (uint8_t)command[2] : -1);
        return true;
    }
}


// T-WA / T-WI take text, T-WD / T-WB the 8 binary bytes T-RD / T-RB return.
// The clock is the system clock; there is no RTC to keep it across a reboot.
void CbmDos::cmdSetTime(const std::string& command, size_t colon)
{
    struct tm t = {};
    bool ok = false;
    char form = command.size() > 3 ? command[3] : 0;

    if ( form == 'A' || form == 'I' )
    {
        // The text follows the four command letters (sd2iec: T-WA<text>). Not
        // the first colon: the time itself has colons, so that took "04:05"
        // out of "T-WI2030-01-02T03:04:05". One leading ':' is allowed.
        (void) colon;
        std::string text = command.substr(4);
        if ( !text.empty() && text[0] == ':' )
            text.erase(0, 1);
        mstr::trim(text);
        ok = form == 'A' ? TimeConverter::fromAsciiTime((const uint8_t *) text.data(), text.size(), &t)
                         : TimeConverter::fromIsoTime((const uint8_t *) text.data(), text.size(), &t);
    }
    else if ( (form == 'D' || form == 'B') && m_raw != nullptr && m_rawLen > 4 )
    {
        // raw: any of the eight bytes may be $0D
        ok = form == 'D' ? TimeConverter::fromDecimalTime(m_raw + 4, m_rawLen - 4, &t)
                         : TimeConverter::fromBcdTime(m_raw + 4, m_rawLen - 4, &t);
    }
    else
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }

    time_t when = ok ? mktime(&t) : (time_t) -1;
    if ( when == (time_t) -1 )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }

    struct timeval tv = { when, 0 };
    settimeofday(&tv, nullptr);
    Debug_printv("clock set to %s", ctime(&when));
    m_host.dosSetStatus(ST_OK);
}



// G-P, R-P, D-P on media with no partition table: 31. CmdHdDos overrides.
void CbmDos::cmdGetPartitionInfo() { m_host.dosSetStatus(ST_SYNTAX_INVALID); }
void CbmDos::cmdRenamePartition(const std::string& arg) { (void) arg; m_host.dosSetStatus(ST_SYNTAX_INVALID); }
void CbmDos::cmdDeletePartition() { m_host.dosSetStatus(ST_SYNTAX_INVALID); }

#endif // BUILD_IEC
