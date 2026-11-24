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

#if defined(BUILD_IEC)

#include "cbm_dos.h"
#include "open_spec.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include "../../drive.h"

#include "mlConfig.h"
#include "fnSystem.h"
#include "led.h"
#include "utils.h"
#include "time_converter.h"

#include "../../../../../include/debug.h"
#include "../../../../../include/cbm_defines.h"

// Moved from iecDrive::executeData() unchanged: every status code, status
// text and side effect is the same. Member accesses go through DosHost.

static bool isMatch(std::string name, std::string pattern)
{
    signed char found = -1;
    
    for(uint8_t i=0; found<0; i++)
    {
        if( pattern[i]=='*' )
            found = 1;
        else if( pattern[i]==0 || name[i]==0 )
            found = (pattern[i]==name[i]) ? 1 : 0;
        else if( pattern[i]!='?' && pattern[i]!=name[i] && !(name[i]=='~' && (pattern[i] & 0xFF)==0xFF) )
            found = 0;
    }

    return found==1;
}

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


void CbmDos::execute(std::string command, const uint8_t* raw, uint8_t rawLen)
{
    m_raw = raw;
    m_rawLen = rawLen;

    if ( cmdMeatloafPrefix(command) )
        return;

    // Drive level commands
    // CBM DOS 2.6
    uint8_t media = 0; 
    // N:, NEW:, N0:, NEW0:, S:, S0, I:, I0:, etc
    size_t colon_position = command.find_first_of(':');
    if (colon_position == std::string::npos)
        colon_position = 0;

    if (colon_position > 1)
    {
        media = atoi((char *) &command[colon_position - 1]);
    }
    Debug_printv("media[%d] colon_position[%d] command[%s]", media, colon_position, command.c_str());

    if ( cmdCbm(command, media, colon_position) )
        return;

    // SD2IEC Commands
    // http://www.n2dvm.com/UIEC.pdf
    if ( cmdSd2iec(command, colon_position) )
        return;

    cmdMeatloafExtended(command);
}


// CD, the E?+/- fast-loader switches and the Dolphin M-R hack.
bool CbmDos::cmdMeatloafPrefix(std::string& command)
{
    if( mstr::startsWith(command, "CD") )
    {
        command = mstr::toUTF8(command);
        m_host.dosSetCwd(mstr::drop(command, 2));
        return true;
    }
#ifdef IEC_FP_JIFFY
    else if( command=="EJ+" || command=="EJ-" )
    {
        m_host.dosEnableFastLoader(IEC_FP_JIFFY, command[2]=='+');
        return true;
    }
#endif  
#ifdef IEC_FP_EPYX
    else if( command=="EE+" || command=="EE-" )
    {
        m_host.dosEnableFastLoader(IEC_FP_EPYX, command[2]=='+');
        return true;
    }
#endif  
#ifdef IEC_FP_AR6
    else if( command=="EA+" || command=="EA-" )
    {
        m_host.dosEnableFastLoader(IEC_FP_AR6, command[2]=='+');
        return true;
    }
#endif
#ifdef IEC_FP_FC3
    else if( command=="EF+" || command=="EF-" )
    {
        m_host.dosEnableFastLoader(IEC_FP_FC3, command[2]=='+');
        return true;
    }
#endif
#ifdef IEC_FP_DOLPHIN
    else if( command=="ED+" || command=="ED-" )
    {
        m_host.dosEnableFastLoader(IEC_FP_DOLPHIN, command[2]=='+');
        return true;
    }
    else if( command == "M-R\xfa\x02\x03" )
    {
        // hack: DolphinDos' MultiDubTwo copy program reads 02FA-02FC to determine
        // number of free blocks => pretend we have 664 (0298h) blocks available
        m_host.dosSetStatus(ST_OK);
        uint8_t data[3] = {0x98, 0, 0x02};
        m_host.dosSetStatusData((char *) data, 3);
        return true;
    }
#endif
    // else
    // {
    //   setStatusCode(ST_SYNTAX_INVALID);
    //   //Debug_printv("Invalid command");
    // }

    return false;
}


// Drive level commands, CBM DOS 2.6.
bool CbmDos::cmdCbm(std::string& command, uint8_t media, size_t colon_position)
{
    (void) media;

    switch ( command[0] )
    {
        case 'B':
            if (command[1] == '-')
            {
                // B-P buffer pointer
                if (command[2] == 'P')
                {
                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");

                    std::vector<std::string> pt = util_tokenize(command, ' ');
                    uint32_t chan = 0, pos = 0;
                    if ( pt.size() < 2 || !dosParseUint(pt[0], &chan) || !dosParseUint(pt[1], &pos) )
                    {
                        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                        return true;
                    }

                    Debug_printv("B-P channel[%lu] position-in-block[%lu]",
                                 (unsigned long) chan, (unsigned long) pos);

                    m_host.dosPositionChannel( chan, pos, true );
                    return true;
                }
                // B-R read block / B-W write block
                else if (command[2] == 'R' || command[2] == 'W')
                {
                    bool for_write = (command[2] == 'W');
                    Debug_printv("%s block", for_write ? "write" : "read");

                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");
                    std::vector<uint8_t> pti = util_tokenize_uint8(command);
                    if ( pti.size() < 4 )
                    {
                        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                        return true;
                    }
                    Debug_printv("command[%s] channel[%d] media[%d] track[%d] sector[%d]", command.c_str(), pti[0], pti[1], pti[2], pti[3]);

                    // CBM keeps byte 0 of a B-R/B-W block as the count of
                    // valid bytes, so both land on byte 1; U1/U2 give the
                    // whole 256.
                    m_host.dosSeekChannelToBlock( pti[0], pti[2], pti[3], 1, for_write );
                    return true;
                }
                // B-A allocate block in BAM
                else if (command[2] == 'A')
                {
                    Debug_printv("allocate block in BAM");

                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");
                    std::vector<uint8_t> pti = util_tokenize_uint8(command);
                    if (pti.size() < 3)
                    {
                        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                        return true;
                    }
                    Debug_printv("track[%d] sector[%d]", pti[0], pti[1]);

                    std::unique_ptr<MFile> image(MFSOwner::File(m_host.dosCwd()->url));
                    auto stream = image ? image->getSourceStream() : nullptr;
                    if (stream == nullptr || !stream->isOpen() || !image || !image->isWritable)
                    {
                        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
                        return true;
                    }

                    // B-A allocates: true = allocate (mark as used)
                    if (!stream->setBlockAllocation(pti[0], pti[1], true))
                    {
                        m_host.dosSetStatus(ST_NO_BLOCK, pti[0], pti[1]);
                        return true;
                    }
                    m_host.dosSetStatus(ST_OK);
                    return true;
                }
                // B-F free block in BAM
                else if (command[2] == 'F')
                {
                    Debug_printv("free block in BAM");

                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");
                    std::vector<uint8_t> pti = util_tokenize_uint8(command);
                    if (pti.size() < 3)
                    {
                        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                        return true;
                    }
                    Debug_printv("track[%d] sector[%d]", pti[0], pti[1]);

                    std::unique_ptr<MFile> image(MFSOwner::File(m_host.dosCwd()->url));
                    auto stream = image ? image->getSourceStream() : nullptr;
                    if (stream == nullptr || !stream->isOpen() || !image || !image->isWritable)
                    {
                        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
                        return true;
                    }

                    // B-F frees: false = deallocate (mark as free)
                    if (!stream->setBlockAllocation(pti[0], pti[1], false))
                    {
                        m_host.dosSetStatus(ST_NO_BLOCK, pti[0], pti[1]);
                        return true;
                    }
                    m_host.dosSetStatus(ST_OK);
                    return true;
                }
                // B-E block execute
                else if (command[2] == 'E')
                {
                    Debug_printv( "block execute");
                }
            }
            //Error(ERROR_31_SYNTAX_ERROR);
            Debug_printv( "block/buffer");
        break;
        case 'C':
            if ( command[1] != 'D' && colon_position)
            {
                //Copy(); // Copy File
                Debug_printv( "copy file");
            }
        break;
        case 'D':
            Debug_printv( "duplicate disk");
            //Error(ERROR_31_SYNTAX_ERROR);	// DI, DR, DW not implemented yet
        break;
        case 'F':
            if (command[1] == '-')
            {
                // F-P file position
                if (command[2] == 'P')
                {
                    command = mstr::drop(command, 3);
                    mstr::trim(command);
                    mstr::replaceAll(command, "  ", " ");

                    // Parsed as 32-bit, not through util_tokenize_uint8():
                    // F-P's argument is an absolute offset in the file and a
                    // uint8_t silently WRAPS it -- 300 became 44, answering OK.
                    std::vector<std::string> pt = util_tokenize(command, ' ');
                    uint32_t chan = 0, pos = 0;
                    if ( pt.size() < 2 || !dosParseUint(pt[0], &chan) || !dosParseUint(pt[1], &pos) )
                    {
                        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                        return true;
                    }

                    Debug_printv("F-P channel[%lu] position[%lu]",
                                 (unsigned long) chan, (unsigned long) pos);

                    m_host.dosPositionChannel( chan, pos, false );
                    return true;
                }
            }
            // Without this an unmatched F-* falls straight into 'I' and
            // resets the drive.
        break;
        case 'I':
            // Initialize
            Debug_printv( "initialize");
            m_host.dosReset();
            return true;
        break;
        case 'M':
            if ( command[1] == '-' ) // Memory
            {
                if (command[2] == 'R') // M-R memory read
                {
                    command = mstr::drop(command, 3); // Drop M-R
                    std::string code = mstr::toHex(command);
                    uint16_t address = (command[0] | command[1] << 8);
                    uint8_t size = command[2];
                    Debug_printv("Memory Read [%s]", code.c_str());
                    Debug_printv("address[%04X] size[%d]", address, size);

                    // size is uint8_t, so 255 is the most that can be asked for.
                    uint8_t data[256] = { 0 };
                    // read() answers with what it could actually supply --
                    // publishing `size` bytes regardless would hand the caller
                    // buffer contents for an address that is not mapped.
                    size_t got = m_host.dosMemory().read(address, data, size);
                    m_host.dosSetStatusData((char *) data, got);
                    Debug_printv("address[%04X] data[%s]", address, mstr::toHex(data, got).c_str());
                }
                else if (command[2] == 'W') // M-W memory write
                {
                    command = mstr::drop(command, 3); // Drop M-W
                    uint16_t address = (command[0] | command[1] << 8);
                    size_t size = command[2]; // Limited to 34 data bytes per command

                    command = mstr::drop(command, 3); // Drop address, size

                    if (size > command.size())
                        size = command.size();

                    Debug_printv("Memory Write address[%04X] size[%d] data[%s]", address, size,
                                 mstr::toHex((const uint8_t *)command.c_str(), size).c_str());

                    m_host.dosMemory().write(address, (const uint8_t *)command.c_str(), size);
                }
                else if (command[2] == 'E') // M-E memory execute
                {
                    command = mstr::drop(command, 3); // Drop M-E
                    std::string code = mstr::toHex(command);
                    uint16_t address = (command[0] | command[1] << 8);
                    Debug_printv("Memory Execute address[%04X][%s]", address, code.c_str());

                    // Fast loader detection happens earlier, in
                    // IECFileDevice::isFastLoaderRequest -- an M-E only reaches
                    // here when no loader claimed it.
                    m_host.dosMemory().execute(address);
                }
                //setStatusCode(ST_OK);
                return true;
            }
        break;
        case 'N':
        {
//#ifdef USE_VDRIVE
            if ( m_host.dosUseVDrive() )
                {
                std::string params = command.substr(colon_position+1);
                size_t dot = params.find('.');

                if( dot==std::string::npos )
                {
                    // must have extension to determine image type
                    m_host.dosSetStatus(ST_SYNTAX_INVALID);
                }
                else
                {
                    std::string filename, diskname, ext;
                    size_t comma = params.find(',');
                    if( comma==std::string::npos )
                    {
                        filename = mstr::toUTF8(params);
                        diskname = params.substr(0,dot);
                    }
                    else
                    {
                        filename = mstr::toUTF8(params.substr(0, comma));
                        diskname = params.substr(comma+1);
                    }

                    fnLedManager.set(eLed::LED_BUS, true);
                    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s]", m_host.dosCwd()->url.c_str(), filename.c_str());
                    MFile *f = m_host.dosCwd()->cd(filename);
                    if( f->exists() )
                        m_host.dosSetStatus(ST_FILE_EXISTS);
                    else if( !f->isWritable )
                        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
                    else if( !m_host.dosCreateDiskImage(f->url, diskname) )
                        m_host.dosSetStatus(ST_WRITE_VERIFY);
                    else
                        m_host.dosSetStatus(ST_OK);
                    fnLedManager.set(eLed::LED_BUS, false);
                }
            }
            else
            {
//#else
                //New();
                Debug_printv( "new (format)");
                command = mstr::toUTF8(command.substr(colon_position + 1));
                if (!m_host.dosCwd()->format(command))
                    m_host.dosSetStatus(ST_WRITE_VERIFY);

                return true;
            }
//#endif
        }
        break;
        case 'R':
            if ( command[1] != 'D' && colon_position ) // Rename
            {
                Debug_printv( "rename file");
                // Rename();
                command = command.substr(colon_position + 1);
                command = mstr::toUTF8(command);
                auto parts = mstr::split(command, '=');

                if( parts.size() != 2 )
                {
                m_host.dosSetStatus(ST_SYNTAX_INVALID);
                return true;
                }

                uint8_t n = 0;
                MFile *dir = MFSOwner::File(m_host.dosCwd()->url);
                if( dir!=nullptr )
                {
                    if( dir->isDirectory() )
                        {
                        std::unique_ptr<MFile> entry;
                        while( (entry=std::unique_ptr<MFile>(m_host.dosCwd()->getNextFileInDir()))!=nullptr )
                            {
                            if( isMatch(entry->name, parts[1]) )
                                {
                                entry->rename(parts[0]);
                                Debug_printv("Renamed '%s' to '%s'", parts[1].c_str(), parts[0].c_str());
                                n++;
                                break;
                                }
                            }
                        }
                    delete dir;
                }

                if ( n == 0 )
                    m_host.dosSetStatus(ST_FILE_NOT_FOUND);

                return true;
            }
        break;
        case 'S': // Scratch
            if ( colon_position )
            {
                Debug_printv( "scratch");
                //Scratch();
                // SCRATCH (delete)
                uint8_t n = 0;

                // Everything between the command letter and the colon is a
                // PATH ("S/TEMPORARY/:*" scratches inside TEMPORARY), and it
                // used to be discarded - the pattern was then matched against
                // the CURRENT directory, so C64 OS clearing its temp directory
                // instead matched os/BOOTER. Nothing was lost only because
                // MFile::remove() refuses a container-hosted entry; on a
                // writable image it would have deleted the wrong files.
                std::string dirPath = command.substr(1, colon_position - 1);
                std::string pattern = command.substr(colon_position + 1);

                // drive number prefix ("S0:...")
                while( !dirPath.empty() && isdigit((unsigned char)dirPath.front()) )
                    dirPath.erase(0, 1);

                // As in open(): one leading '/' descends from the current
                // directory, "//" starts at the root of the image.
                if( dirPath.size() > 1 && dirPath[0] == '/' && dirPath[1] != '/' )
                    dirPath.erase(0, 1);
                while( !dirPath.empty() && dirPath.back() == '/' )
                    dirPath.pop_back();

                std::unique_ptr<MFile> dir(dirPath.empty()
                                           ? MFSOwner::File(m_host.dosCwd()->fullUrl())
                                           : m_host.dosCwd()->cd(dirPath));
                if( dir!=nullptr && dir->isDirectory() )
                {
                    std::unique_ptr<MFile> entry;
                    while( (entry=std::unique_ptr<MFile>(dir->getNextFileInDir()))!=nullptr )
                        {
                        if( !entry->isDirectory() && isMatch(mstr::toPETSCII2(entry->name), pattern) )
                            {
                            Debug_printv("DELETING %s", entry->name.c_str());
                            if( entry->remove() ) n++;
                            }
                        //else Debug_printv("NOT DELETING %s", entry->name.c_str());
                        }
                }

                m_host.dosSetStatus(ST_SCRATCHED, n);
                return true;
            }
        break;
        case 'U':
            if (command[1] == '1' || command[1] == '2' || command[1] == 'A' || command[1] == 'B') // U1/UA, U2/UB
            {
                // Block Read if 1/A, Write if 2/B
                bool read = (command[1] == '1' || command[1] == 'A');
                command = mstr::drop(command, 3);
                mstr::trim(command);
                mstr::replaceAll(command, "  ", " ");
                std::vector<uint8_t> pti = util_tokenize_uint8(command);
                if ( pti.size() < 4 )
                {
                    m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                    return true;
                }
                Debug_printv("command[%s] channel[%d] media[%d] track[%d] sector[%d]", command.c_str(), pti[0], pti[1], pti[2], pti[3]);

                // TODO: Check for C128 Auto boot and if track/sector is 1/0

                // Unlike B-R/B-W, U1/U2 give the WHOLE block -- there is no
                // count byte -- so they land on byte 0.
                //
                // The seek matters beyond the data: some fast loaders expect
                // the drive's track/sector registers to be updated here and
                // then issue reads that do not name a block.
                Debug_printv("Block-%s", read ? "Read" : "Write");
                m_host.dosSeekChannelToBlock( pti[0], pti[2], pti[3], 0, !read );
                return true;
            }
            else if ((command[1] >= '3' && command[1] <= '9') || (command[1] >= 'C' && command[1] <= 'H')) // U3-U9, UC-UH
            {
                uint8_t offset = command[1] - '3';
                if (offset > 5)
                {
                    //Debug_printv("U%C [%02x] C[%02x]", command[1], command[1], 'C');
                    offset = command[1] - 'C';
                }
                offset = (offset * 3);
                //Debug_printv( "U%c, offset %02x", command[1], offset);
                m_host.dosMemory().execute(0x0500 + offset);
                return true;
            }
            else if ((command[1] == '9' || command[1] == 'I') && command.size() == 2) // U9, UI
            {
                Debug_printv( "warm reset");
                m_host.dosSetStatus(ST_DOSVERSION);
                m_host.dosMemory().execute(0xFFFA);
                return true;
            }
            else if (command[1] == 'J') // UJ
            {
                Debug_printv( "cold reset");
                m_host.dosReset();
                return true;
            }
            else if (command[1] == 0xCA) // U{Shift-J}
            {
                if ( command[2] == '+')
                {
                    Debug_printv( "reboot");
                    fnSystem.reboot();
                    return true;
                }
                Debug_printv( "hard reset");
                m_host.dosReset();
                //m_cwd.reset(m_cwd->cd("^")); // reset to flash root
                m_host.dosSetCwd("^");
                return true;
            }
            else if (command[1] == 'I' && command.size() == 3) // UI+/-
            {
                if (command[2] == '-')
                {
                    Debug_printv( "VIC-20 Bus Speed");
                    // Set IEC Data Valid timing to 20us
                }
                else
                {
                    Debug_printv( "C64 Bus Speed");
                    // Set IEC Data Valid timing to 60us
                }
                //setStatusCode(ST_OK);
                return true;
            }
        break;
        case 'V':
            {
                Debug_printv( "validate bam");

                std::unique_ptr<MFile> image(MFSOwner::File(m_host.dosCwd()->url));
                auto stream = image ? image->getSourceStream() : nullptr;
                if (stream == nullptr || !stream->isOpen() || !image || !image->isWritable)
                {
                    m_host.dosSetStatus(ST_DRIVE_NOT_READY);
                    return true;
                }

                if (!stream->validateBAM())
                {
                    m_host.dosSetStatus(ST_DRIVE_NOT_READY);
                    return true;
                }
                return true;
            }
        break;
        default:
            //Error(ERROR_31_SYNTAX_ERROR);
        break;
    }

    return false;
}


// SD2IEC Commands
// http://www.n2dvm.com/UIEC.pdf
bool CbmDos::cmdSd2iec(std::string& command, size_t colon_position)
{
    switch ( command[0] )
    {
        case 'C':
            if ( command[1] == 'P' || command[1] == 'p') // CP<n> / C{Shift-P}<byte>
                return cmdChangePartition(command);
            // else if ( command[1] == 'D') // Change Directory
            // {
            //     set_cwd(mstr::drop(command, 2));
            // }
        break;
        case 'E':
            if (command[1] == '-')
            {
                Debug_printv( "eeprom");
            }
        break;
        case 'G':
            Debug_printv( "get partition info");
            //Error(ERROR_31_SYNTAX_ERROR);	// G-P not implemented yet
        break;
        case 'M':
            if ( command[1] == 'D') // Make Directory
            {
                std::string path = mstr::toUTF8(command.substr(colon_position + 1));
                Debug_printv( "make directory path[%s]", path.c_str());
                MFile *f = m_host.dosCwd()->cd( path );
                if( f!=nullptr )
                {
                    bool created = false;
                    if( f->sourceFile!=nullptr )
                    {
                        if( f->sourceFile->exists() )
                        {
                            Debug_printv("directory exists");
                            m_host.dosSetStatus(ST_FILE_EXISTS);
                        }
                        else if( !f->sourceFile->isWritable )
                        {
                            Debug_printv("not writable");
                            m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
                        }
                        else if( f->format("meatloaf,01") )
                        {
                            Debug_printv("format ok");
                            created = true;
                        }
                    }

                    if ( !created )
                    {
                        if( f->exists() )
                        {
                            Debug_printv("directory exists");
                            m_host.dosSetStatus(ST_FILE_EXISTS);
                        }
                        else if( !f->isWritable )
                        {
                            Debug_printv("not writable");
                            m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
                        }
                        else if( f->mkDir() )
                        {
                            Debug_printv("mkdir ok");
                        }
                        else
                        {
                            Debug_printv("make directory failed");
                            m_host.dosSetStatus(ST_WRITE_VERIFY);
                        }
                    }

                    delete f;
                }
                else
                {
                    Debug_printv("make directory failed");
                    m_host.dosSetStatus(ST_WRITE_VERIFY);
                }
                return true;
            }
        break;
        case 'P':
            {
                // PWD - print working directory
                if ( command[1] == 'W' && command[2] == 'D' )
                {
                    Debug_printv( "print working directory");
                    std::string path = mstr::toPETSCII2(m_host.dosCwd()->url);
                    path += "\r";
                    m_host.dosSetStatusData((const char *) path.c_str(), path.length());
                    return true;
                }

                // Position a REL channel: P <$60+channel> <record lo> <record hi>
                // <byte>, binary, as vdrive_command_position decodes it. The raw
                // bytes are used: any parameter may be $0D, which the command
                // string has already lost when it was the last byte.
                PositionCommand pc;
                if ( !parsePositionCommand(m_raw, m_rawLen, pc) )
                {
                    m_host.dosSetStatus(ST_NO_CHANNEL);
                    return true;
                }
                Debug_printv("position channel[%d] record[%d] byte[%d]", pc.channel, pc.record, pc.position);
                auto channel = m_host.dosChannel(pc.channel);
                RELMStream *rel = channel != nullptr ? channel->relStream() : nullptr;
                if ( rel == nullptr )
                {
                    // vdrive_command_position: not a relative buffer
                    m_host.dosSetStatus(ST_NO_CHANNEL);
                    return true;
                }
                uint8_t st = rel->position(pc.record, pc.position);
                if ( st != ST_OK )
                    m_host.dosSetStatus(st);
                // bytes already read ahead belong to the old record
                m_host.dosClearReadBuffer(pc.channel);
                uint32_t record = pc.record ? pc.record - 1 : 0;
                uint32_t byte   = pc.position ? pc.position - 1 : 0;
                channel->repositioned(record * rel->recordLength() + byte);
                return true;
            }
        break;
        case 'R':
            if ( command[1] == 'D') // Remove Directory
            {
                Debug_printv( "remove directory");
                std::string path = mstr::toUTF8(command.substr(colon_position + 1));
                MFile *f = m_host.dosCwd()->cd( path );
                if( f!=nullptr )
                    {
                    if( !f->exists() )
                        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
                    else if( !f->isWritable )
                        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
                    else if( !f->rmDir() )
                        m_host.dosSetStatus(ST_WRITE_VERIFY);

                    delete f;
                    }
                return true;
            }
        break;
        case 'S':
            if (command[1] == '-')
            {
                if (command[2] == 'D' || mstr::isNumeric(&command[2])) // Swap Drive
                {
                    // Swap drive number 
                    Debug_printv( "swap drive number [%c]", command[2]);
                    return true;
                }
                else if (command[2] == 'C') // Send SCSI Command
                {
                    // Set partition number
                    Debug_printv( "send scsi command");
                    return true;
                }
            }
        break;
        case 'T':
            if (command[1] == '-')
            {
                // Tape image commands (current directory is a mounted tape):
                // T-C <ms|MMM:SS> - set the tape counter (read position)
                // T-I             - scan the tape and generate its .idx file
#ifndef DISABLE_TAPE   // MEATLOAF-GATE
                if (command[2] == 'C' || command[2] == 'I')
                {
                    m_host.dosTapeCommand(command);
                    return true;
                }
#endif

                time_t tt = time(nullptr);
                struct tm *tinfo = localtime(&tt);

                uint8_t buf[IECFILEDEVICE_STATUS_BUFFER_SIZE];
                size_t len = 0;

                //Error(ERROR_31_SYNTAX_ERROR);	// T-R and T-W not implemented yet
                if (command[2] == 'R')
                {
                    if(command[3] == 'A')
                    {
                        Debug_printv("read ascii format");
                        len = TimeConverter::toAsciiTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'I')
                    {
                        Debug_printv("read ISO-8601 format");
                        len = TimeConverter::toIsoTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'D')
                    {
                        Debug_printv("read decimal format");
                        len = TimeConverter::toDecimalTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'B')
                    {
                        Debug_printv("read BCD format");
                        len = TimeConverter::toBcdTime(tinfo, buf, IECFILEDEVICE_STATUS_BUFFER_SIZE);
                    }
                    else if(command[3] == 'Z')
                    {
                        std::string tz;
                        char* tz_value = getenv("TZ");
                        if (tz_value != nullptr) {
                            tz = tz_value;
                        } else {
                            tz = "UTC"; // Set a default value
                        }
                        Debug_printv("read TIMEZONE [%s]", tz.c_str());
                        tz = mstr::toPETSCII2(tz);
                        len = tz.size();
                        memcpy(buf, tz.data(), len);
                    }
                    m_host.dosSetStatusData((const char *) buf, len);
                }
                else if (command[2] == 'W')
                {
                    if(command[3] == 'A')
                    {
                        Debug_printv("write ascii format");
                    }
                    else if(command[3] == 'I')
                    {
                        Debug_printv("write ISO-8601 format");
                    }
                    else if(command[3] == 'D')
                    {
                        Debug_printv("write decimal format");
                    }
                    else if(command[3] == 'B')
                    {
                        Debug_printv("write BCD format");
                    }
                    else if(command[3] == 'Z')
                    {
                        if (colon_position)
                            command = command.substr(colon_position + 1);
                        else
                            command = mstr::drop(command, 4);
                        command = mstr::toUTF8(command);
                        mstr::trim(command);
                        mstr::replaceAll(command, " ", "_");
                        Debug_printv("write TIMEZONE [%s]", command.c_str());

                        const char *posix_tz = iana_to_posix_tz(command);
                        setenv("TZ", posix_tz ? posix_tz : command.c_str(), 1);
                        tzset();

                        // Persist the IANA name (not the resolved POSIX
                        // string) so mlConfig.load() re-resolves it the
                        // same way on the next boot.
                        json_object_at(mlConfig.data(), "preferences")["timezone"] = command;
                        mlConfig.save();
                    }
                }
                return true;
            }
        break;
        case 'W':
            if (command[1] == '-') // Write Protect
            {
                if (command[2] == '0') // W-0 disable write protect
                {
                    Debug_printv( "write protect disabled");
                    return true;
                }
                else if (command[2] == '1') // W-1 enable write protect
                {
                    Debug_printv( "write protect enabled");
                    return true;
                }
            }
        break;
        case 'X':
            Debug_printv( "xtended commands");
            // X{0-4}
            // XE+ / XE-
            // XB+ / XB-
            // X{0-7}={0-15}
            // XD?
            // XJ+ / XJ-
            // XF+ / XF-
            // X
            // XR:{name}
            if (command[1] == 'R' && colon_position)
            {
                command = command.substr(colon_position + 1);
                Debug_printv("rom[%s]", command.c_str());
                command = mstr::toUTF8(command);
                if (!m_host.dosMemory().setROM(command))
                {
                    m_host.dosSetStatus(ST_FILE_NOT_FOUND);
                    break;
                }
                //setStatusCode(ST_OK);
                return true;
            }
            // XS:{name} / XS
            // XW
            // X?
            //Extended();
        break;
        case '/':

        break;
        default:
            //Error(ERROR_31_SYNTAX_ERROR);
        break;
    }

    return false;
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
