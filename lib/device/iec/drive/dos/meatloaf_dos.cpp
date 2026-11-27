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

// The command channel, part 3 of 3: Meatloaf's own commands. CbmDos::execute()
// tries cmdMeatloafPrefix() FIRST (these must win over a CBM command that
// starts with the same letters) and cmdMeatloafExtended() LAST.
//
//   cbm_dos.cpp       CBM DOS 2.6 / 1581
//   sd2iec_dos.cpp    SD2IEC and CMD extensions
//   meatloaf_dos.cpp  this file

#if defined(BUILD_IEC)

#include "cbm_dos.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../drive.h"
#include "../../../../meatloaf/media/hd/dhd.h"

#include "fnSystem.h"
#include "utils.h"

#include "../../../../../include/debug.h"
#include "../../../../../include/cbm_defines.h"

// Parse one decimal DOS command argument.  strtoul, never stoi/stol: ESP-IDF
// builds -fno-exceptions, so a throwing parser on a string that came off the
// IEC bus is std::terminate.  Rejects an empty string and any trailing
// rubbish rather than accepting a prefix, so a malformed argument is a syntax
// error instead of a silently different number.
static bool dosParseUint(const std::string &s, uint32_t *out)
{
    if ( s.empty() ) return false;

    char *end = nullptr;
    unsigned long value = strtoul(s.c_str(), &end, 10);
    if ( end == s.c_str() || *end != '\0' ) return false;

    *out = (uint32_t) value;
    return true;
}


// CD, the E?+/- fast-loader switches, the Dolphin M-R hack, F-P, U{Shift-J}
// and the tape commands T-C/T-I. Returns false for anything else.
bool CbmDos::cmdMeatloafPrefix(std::string& command)
{
    if ( mstr::startsWith(command, "CD") )
    {
        command = mstr::toUTF8(command);
        m_host.dosSetCwd(mstr::drop(command, 2));
        return true;
    }

    if ( command.size() == 3 && command[0] == 'E' && (command[2] == '+' || command[2] == '-') )
        return cmdFastLoaderSwitch(command);

#ifdef IEC_FP_DOLPHIN
    if ( command == "M-R\xfa\x02\x03" )
    {
        // hack: DolphinDos' MultiDubTwo copy program reads 02FA-02FC to determine
        // number of free blocks => pretend we have 664 (0298h) blocks available
        m_host.dosSetStatus(ST_OK);
        uint8_t data[3] = {0x98, 0, 0x02};
        m_host.dosSetStatusData((char *) data, 3);
        return true;
    }
#endif

    if ( mstr::startsWith(command, "F-P") )
        return cmdFilePosition(command);

    if ( command.size() >= 2 && command[0] == 'U' && (uint8_t) command[1] == 0xCA )
    {
        cmdHardReset(command);
        return true;
    }

#ifndef DISABLE_TAPE   // MEATLOAF-GATE
    // Tape image commands (current directory is a mounted tape):
    // T-C <ms|MMM:SS> - set the tape counter (read position)
    // T-I             - scan the tape and generate its .idx file
    if ( command.size() >= 3 && command[0] == 'T' && command[1] == '-' &&
         (command[2] == 'C' || command[2] == 'I') )
    {
        m_host.dosTapeCommand(command);
        return true;
    }
#endif

    return false;
}

// EJ+/- JiffyDOS, EE+/- Epyx, EA+/- Action Replay 6, EF+/- Final Cartridge 3,
// ED+/- DolphinDOS: enable or disable a fast loader. A family this build does
// not include is not a Meatloaf command (falls through).
bool CbmDos::cmdFastLoaderSwitch(const std::string& command)
{
    bool enable = command[2] == '+';
    switch ( command[1] )
    {
#ifdef IEC_FP_JIFFY
        case 'J': m_host.dosEnableFastLoader(IEC_FP_JIFFY, enable);   return true;
#endif
#ifdef IEC_FP_EPYX
        case 'E': m_host.dosEnableFastLoader(IEC_FP_EPYX, enable);    return true;
#endif
#ifdef IEC_FP_AR6
        case 'A': m_host.dosEnableFastLoader(IEC_FP_AR6, enable);     return true;
#endif
#ifdef IEC_FP_FC3
        case 'F': m_host.dosEnableFastLoader(IEC_FP_FC3, enable);     return true;
#endif
#ifdef IEC_FP_DOLPHIN
        case 'D': m_host.dosEnableFastLoader(IEC_FP_DOLPHIN, enable); return true;
#endif
        default: (void) enable; return false;
    }
}

// F-P <channel> <position>: an absolute offset into the file or container.
bool CbmDos::cmdFilePosition(std::string command)
{
    command = mstr::drop(command, 3);
    mstr::trim(command);
    mstr::replaceAll(command, "  ", " ");

    // Parsed as 32-bit, not through util_tokenize_uint8(): F-P's argument is
    // an absolute offset in the file and a uint8_t silently WRAPS it -- 300
    // became 44, answering OK.
    std::vector<std::string> pt = util_tokenize(command, ' ');
    uint32_t chan = 0, pos = 0;
    if ( pt.size() < 2 || !dosParseUint(pt[0], &chan) || !dosParseUint(pt[1], &pos) )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return true;
    }

    Debug_printv("F-P channel[%lu] position[%lu]", (unsigned long) chan, (unsigned long) pos);
    m_host.dosPositionChannel( chan, pos, false );
    return true;
}


// partition:new,<file>[,<MB>] creates an empty CMD container (.dhd .d1m .d2m
// .d4m; a DHD needs its size in MB) in the current directory - or, when the
// drive stands in a CMD image, beside it. Every other partition:... verb
// needs a partition table, so on other media it is 31 (CmdHdDos has them).
void CbmDos::cmdPartitionAdmin(const std::string& args)
{
    std::vector<std::string> a = mstr::split(args, ',');
    for ( auto &x : a )
        mstr::trim(x);
    if ( a.empty() || a[0] != "new" )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }
    if ( a.size() < 2 || a.size() > 3 || a[1].empty() )
    {
        m_host.dosSetStatus(a.size() < 2 || a[1].empty() ? ST_SYNTAX_NO_NAME : ST_SYNTAX_UNKNOWN);
        return;
    }

    uint32_t mb = 0;
    if ( a.size() == 3 && !dosParseUint(a[2], &mb) )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }

    std::string url = mediaPath(a[1]);
    m_host.dosSetStatus(DHDImageRegistry::createImage(url, mb));
    invalidateListings();
}

// A file named in a partition command: a URL or an absolute path as given,
// anything else relative to the directory the drive is in - or, inside a
// CMD image, the directory that image is in.
std::string CbmDos::mediaPath(const std::string& name)
{
    if ( name.find("://") != std::string::npos || (!name.empty() && name[0] == '/') )
        return name;
    MFile *cwd = m_host.dosCwd();
    std::string dir = cwd ? cwd->fullUrl() : std::string("/");
    std::string container = DHDImageRegistry::containerOf(dir);
    if ( !container.empty() )
    {
        size_t slash = container.find_last_of('/');
        dir = slash == std::string::npos ? std::string() : container.substr(0, slash);
    }
    while ( dir.size() > 1 && dir.back() == '/' )
        dir.pop_back();
    return (dir == "/" ? std::string() : dir) + "/" + name;
}


// U{Shift-J}: hard reset back to the flash root; U{Shift-J}+ reboots.
void CbmDos::cmdHardReset(const std::string& command)
{
    if ( command.size() > 2 && command[2] == '+' )
    {
        Debug_printv("reboot");
        fnSystem.reboot();
        return;
    }
    Debug_printv("hard reset");
    m_host.dosReset();
    m_host.dosSetCwd("^");
}


// auth:user,password - credentials for the current (network) directory.
// Anything else that reached here is unknown: 31.
void CbmDos::cmdMeatloafExtended(std::string& command)
{
    // Meatloaf Extended Commands
    command = mstr::toUTF8(command);
    auto pt = util_tokenize(command, ':');

    // util_tokenize() yields NO tokens for an empty command, and the C64 does
    // send one - a lone 0x00 on the command channel arrives here as "". Reading
    // pt[0] then faults (LoadProhibited comparing a std::string at address 0).
    if ( pt.empty() )
        return;

    // partition:create,... / partition:delete,... / partition:default,...
    // on CMD images (CmdHdDos); 31 everywhere else.
    if ( pt[0] == "partition" )
    {
        size_t colon = command.find(':');
        cmdPartitionAdmin(colon == std::string::npos ? std::string() : command.substr(colon + 1));
        return;
    }

    if ( pt[0] == "auth" )
    {
        if( pt.size() == 2 )
        {
            auto creds = util_tokenize(pt[1], ',');
            if( creds.size() == 2 )
            {
                Debug_printv("Setting authorization for url[%s] user[%s] pass[%s]", m_host.dosCwd()->url.c_str(), creds[0].c_str(), creds[1].c_str());
                m_host.dosCwd()->user = creds[0];
                m_host.dosCwd()->password = creds[1];
                m_host.dosCwd()->rebuildUrl();
                //setStatusCode(ST_OK);
                return;
            }
        }
    }

    m_host.dosSetStatus(ST_SYNTAX_INVALID);
}

#endif // BUILD_IEC
