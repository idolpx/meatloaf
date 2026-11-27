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

// The command channel, part 1 of 3: CBM DOS 2.6 and the 1581 additions, plus
// execute() itself and the helpers the other two parts share.
//
//   cbm_dos.cpp       this file
//   sd2iec_dos.cpp    SD2IEC and CMD extensions
//   meatloaf_dos.cpp  Meatloaf's own commands

#if defined(BUILD_IEC)

#include "cbm_dos.h"
#include "open_spec.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "../../drive.h"
#include "../../../../meatloaf/meat_media.h"

#include "led.h"
#include "utils.h"

#include "../../../../../include/debug.h"
#include "../../../../../include/cbm_defines.h"

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



// Drive level commands, CBM DOS 2.6. Only decides WHICH command it is; each
// command is its own function below. A handler that returns false (or a case
// that breaks) lets the command fall through to the SD2IEC and Meatloaf sets.
bool CbmDos::cmdCbm(std::string& command, uint8_t media, size_t colon_position)
{
    (void) media;

    switch ( command[0] )
    {
        case 'B':
            return cmdBlock(command);

        case 'C':
            // C[d]:new=[d:]old[,[d:]old2...] - CD and CP are handled elsewhere
            if ( command[1] != 'D' && command[1] != 'P' && command[1] != 'p' && colon_position )
            {
                cmdCopy(command.substr(colon_position + 1));
                return true;
            }
            break;

        case 'D':
            // D-P <count> <partition...> $FF: delete CMD FD partitions.
            // D (duplicate) needs a dual drive and answers 31.
            if ( command.size() >= 3 && command[1] == '-' && command[2] == 'P' )
            {
                cmdDeletePartition();
                return true;
            }
            break;

        case 'I':
            cmdInitialize();
            return true;

        case 'L':
            if ( colon_position )
            {
                cmdLock(command.substr(colon_position + 1));
                return true;
            }
            break;

        case 'M':
            if ( command[1] == '-' )
                return cmdMemory(command);
            break;

        case 'N':
            cmdNew(command, colon_position);
            return true;

        case 'P':
            // PWD (SD2IEC) starts with the same letter; only the binary
            // position form is CBM DOS.
            if ( isBarePwd(command) )
                break;
            cmdPosition();
            return true;

        case 'R':
            if ( command[1] == '-' && command.size() > 2 )
            {
                // R-H:name renames the disk header; R-P:new=old a CMD partition
                std::string arg = colon_position ? command.substr(colon_position + 1) : std::string();
                if ( command[2] == 'H' )
                    cmdRenameHeader(arg);
                else if ( command[2] == 'P' )
                    cmdRenamePartition(arg);
                else
                    m_host.dosSetStatus(ST_SYNTAX_INVALID);
                return true;
            }
            if ( command[1] != 'D' && colon_position ) // RD is an SD2IEC command
            {
                cmdRename(command.substr(colon_position + 1));
                return true;
            }
            break;

        case 'S':
            if ( colon_position )
            {
                cmdScratch(command, colon_position);
                return true;
            }
            break;

        case 'U':
            return cmdUser(command);

        case 'V':
            cmdValidate();
            return true;

        case '/':
            cmdSelectSubPartition(command, colon_position);
            return true;

        default:
            break;
    }

    return false;
}


// --- B-x: block commands -----------------------------------------------------

// B-x / BLOCK-x: the letter after the '-' selects the command.
bool CbmDos::cmdBlock(const std::string& command)
{
    size_t dash = command.find('-');
    size_t colon = command.find(':');
    if ( dash == std::string::npos || dash + 1 >= command.size() ||
         (colon != std::string::npos && colon < dash) )
        return false;

    switch ( command[dash + 1] )
    {
        case 'P': cmdBlockPointer(command);           break;
        case 'R': cmdBlockReadWrite(command, false);  break;
        case 'W': cmdBlockReadWrite(command, true);   break;
        case 'A': cmdBlockAllocate(command, true);    break;
        case 'F': cmdBlockAllocate(command, false);   break;
        case 'E': cmdBlockExecute(command);           break;
        default:  m_host.dosSetStatus(ST_SYNTAX_INVALID); break;
    }
    return true;
}

// B-P <channel> <position>
void CbmDos::cmdBlockPointer(const std::string& command)
{
    int p[4] = { 0, 0, 0, 0 };
    if ( !blockParams(command, p, 2) )
        return;
    Debug_printv("B-P channel[%d] position[%d]", p[0], p[1]);
    m_host.dosPositionChannel( p[0], p[1], true );
}

// B-R/B-W <channel> <drive> <track> <sector>: byte 0 of the block is the
// count of valid bytes. U1/U2 give the whole 256.
void CbmDos::cmdBlockReadWrite(const std::string& command, bool for_write)
{
    int p[4] = { 0, 0, 0, 0 };
    if ( !blockParams(command, p, 4) || !trackSectorInRange(p[2], p[3]) )
        return;
    Debug_printv("B-%c channel[%d] drive[%d] track[%d] sector[%d]",
                 for_write ? 'W' : 'R', p[0], p[1], p[2], p[3]);
    if ( m_host.dosBlockTransfer( p[0], p[2], p[3], for_write, true ) && for_write )
        invalidateListings();
}

// B-A/B-F <drive> <track> <sector>: allocate/free in the BAM
void CbmDos::cmdBlockAllocate(const std::string& command, bool allocate)
{
    int p[4] = { 0, 0, 0, 0 };
    if ( !blockParams(command, p, 3) || !trackSectorInRange(p[1], p[2]) )
        return;
    Debug_printv("B-%c drive[%d] track[%d] sector[%d]", allocate ? 'A' : 'F', p[0], p[1], p[2]);

    std::unique_ptr<MFile> image;
    auto stream = openImageForUpdate(image);
    if ( stream == nullptr )
        return;

    uint8_t t = p[1], s = p[2];
    if ( stream->sectorByteOffset(t, s) < 0 )
    {
        m_host.dosSetStatus(ST_ILLEGAL_TS_COMMAND, t, s);
        return;
    }
    if ( !stream->setBlockAllocation(t, s, allocate) )
    {
        // vdrive_command_block 'A': the block is in use, so suggest the next
        // free one (0/0 when there is none).
        uint8_t ft = 0, fs = 0;
        if ( !stream->suggestFreeBlock(t, s, &ft, &fs) )
            ft = fs = 0;
        m_host.dosSetStatus(ST_NO_BLOCK, ft, fs);
        return;
    }
    invalidateListings();
    m_host.dosSetStatus(ST_OK);
}

// B-E <channel> <drive> <track> <sector>: read the block into the buffer and
// run it. There is no 6502 here to run it on; VICE answers "needs TDE" the
// same way.
void CbmDos::cmdBlockExecute(const std::string& command)
{
    int p[4] = { 0, 0, 0, 0 };
    if ( !blockParams(command, p, 4) || !trackSectorInRange(p[2], p[3]) )
        return;
    if ( m_host.dosBlockTransfer( p[0], p[2], p[3], false, false ) )
        m_host.dosSetStatus(ST_UNKNOWN_DRIVECODE);
}


// --- I ------------------------------------------------------------------------


void CbmDos::cmdInitialize()
{
    Debug_printv("initialize");
    m_host.dosReset();
    m_host.dosSetStatus(ST_OK);
}


// P <$60+channel> <record lo> <record hi> <byte>: position a REL channel,
// binary, as vdrive_command_position decodes it. The raw bytes are used: any
// parameter may be $0D, which the command string has already lost when it was
// the last byte.
void CbmDos::cmdPosition()
{
    PositionCommand pc;
    if ( !parsePositionCommand(m_raw, m_rawLen, pc) )
    {
        m_host.dosSetStatus(ST_NO_CHANNEL);
        return;
    }
    Debug_printv("position channel[%d] record[%d] byte[%d]", pc.channel, pc.record, pc.position);
    auto channel = m_host.dosChannel(pc.channel);
    RELMStream *rel = channel != nullptr ? channel->relStream() : nullptr;
    if ( rel == nullptr )
    {
        // vdrive_command_position: not a relative buffer
        m_host.dosSetStatus(ST_NO_CHANNEL);
        return;
    }
    uint8_t st = rel->position(pc.record, pc.position);
    if ( st != ST_OK )
        m_host.dosSetStatus(st);
    // bytes already read ahead belong to the old record
    m_host.dosClearReadBuffer(pc.channel);
    uint32_t record = pc.record ? pc.record - 1 : 0;
    uint32_t byte   = pc.position ? pc.position - 1 : 0;
    channel->repositioned(record * rel->recordLength() + byte);
}

// PWD is only the bare three letters (and BASIC's trailing CR): a binary P
// whose channel byte is $57 and record-low byte $44 starts with the same
// bytes, and it always carries at least the record-high and position.
bool CbmDos::isBarePwd(const std::string& command) const
{
    bool bare = m_rawLen == 3 || (m_rawLen == 4 && m_raw[3] == 0x0D);
    return bare && command.size() >= 3 && command[1] == 'W' && command[2] == 'D';
}


// --- M-x: memory commands ----------------------------------------------------

// vdrive_command_memory: parsed from the RAW bytes. The address, count and data
// are binary, and any of them may be $0D - which the CR-stripped command string
// has lost when it was the last byte (an M-R of 13 bytes, an M-W ending in $0D).
bool CbmDos::cmdMemory(const std::string& command)
{
    if ( m_raw == nullptr || m_rawLen < 5 )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return true;
    }
    uint16_t address = m_raw[3] | (m_raw[4] << 8);

    switch ( command[2] )
    {
        case 'R': cmdMemoryRead(address);  break;
        case 'W': cmdMemoryWrite(address); break;
        case 'E':
            // Fast loader detection happens earlier, in
            // IECFileDevice::isFastLoaderRequest -- an M-E only reaches here
            // when no loader claimed it.
            Debug_printv("Memory Execute address[%04X]", address);
            m_host.dosMemory().execute(address);
            break;
        default:
            m_host.dosSetStatus(ST_SYNTAX_INVALID);
            break;
    }
    return true;
}

// vdrive_command_memory_read: no count byte means one; 0 means 256 (the status
// buffer holds 255 of them).
void CbmDos::cmdMemoryRead(uint16_t address)
{
    unsigned count = m_rawLen < 6 ? 1 : m_raw[5];
    if ( count == 0 ) count = 256;
    if ( count > IECFILEDEVICE_STATUS_BUFFER_SIZE ) count = IECFILEDEVICE_STATUS_BUFFER_SIZE;
    Debug_printv("Memory Read address[%04X] size[%u]", address, count);

    uint8_t data[256] = { 0 };
    // read() answers with what it could actually supply -- publishing `count`
    // bytes regardless would hand the caller buffer contents for an address
    // that is not mapped.
    size_t got = m_host.dosMemory().read(address, data, count);
    m_host.dosSetStatusData((char *) data, got);
    Debug_printv("address[%04X] data[%s]", address, mstr::toHex(data, got).c_str());
}

// vdrive_command_memory_write: the count byte, then that many data bytes, all
// of which must have arrived.
void CbmDos::cmdMemoryWrite(uint16_t address)
{
    if ( m_rawLen < 6 || (unsigned) m_raw[5] + 6 > m_rawLen )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }
    uint8_t count = m_raw[5];
    Debug_printv("Memory Write address[%04X] size[%d] data[%s]", address, count,
                 mstr::toHex(m_raw + 6, count).c_str());
    m_host.dosMemory().write(address, m_raw + 6, count);
}


// --- N, L, S, V ---------------------------------------------------------------

// N[d]:name,id: format. In VDrive mode it creates a new image file instead,
// which needs an extension to say what kind.
void CbmDos::cmdNew(const std::string& command, size_t colon_position)
{
    if ( !m_host.dosUseVDrive() )
    {
        Debug_printv("new (format)");
        std::string spec = mstr::toUTF8(command.substr(colon_position + 1));
        if ( !m_host.dosCwd()->format(spec) )
            m_host.dosSetStatus(ST_WRITE_VERIFY);
        return;
    }

    std::string params = command.substr(colon_position + 1);
    size_t dot = params.find('.');
    if ( dot == std::string::npos )
    {
        // must have extension to determine image type
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }

    std::string filename, diskname;
    size_t comma = params.find(',');
    if ( comma == std::string::npos )
    {
        filename = mstr::toUTF8(params);
        diskname = params.substr(0, dot);
    }
    else
    {
        filename = mstr::toUTF8(params.substr(0, comma));
        diskname = params.substr(comma + 1);
    }

    fnLedManager.set(eLed::LED_BUS, true);
    Debug_printv( ANSI_MAGENTA_BOLD_HIGH_INTENSITY "Changing directory to [%s][%s]", m_host.dosCwd()->url.c_str(), filename.c_str());
    std::unique_ptr<MFile> f(m_host.dosCwd()->cd(filename));
    if ( f->exists() )
        m_host.dosSetStatus(ST_FILE_EXISTS);
    else if ( !f->isWritable )
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
    else if ( !m_host.dosCreateDiskImage(f->url, diskname) )
        m_host.dosSetStatus(ST_WRITE_VERIFY);
    else
        m_host.dosSetStatus(ST_OK);
    fnLedManager.set(eLed::LED_BUS, false);
}

// L[d]:name toggles the lock bit (vdrive_command_lockunlock).
void CbmDos::cmdLock(const std::string& arg)
{
    std::string name = stripDrivePrefix(mstr::toUTF8(arg));
    if ( name.empty() || hasWildcard(name) )
    {
        m_host.dosSetStatus(name.empty() ? ST_SYNTAX_NO_NAME : ST_SYNTAX_UNKNOWN);
        return;
    }
    std::unique_ptr<MFile> f(m_host.dosCwd()->cd(name));
    if ( f == nullptr || !f->exists() )
        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
    else if ( !f->isWritable )
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
    else if ( !f->toggleLock() )
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);   // no lock bit on this media
    else
        m_host.dosSetStatus(ST_OK);
}

// S[path]:pattern - scratch every matching file.
void CbmDos::cmdScratch(const std::string& command, size_t colon_position)
{
    Debug_printv("scratch");
    uint8_t n = 0;

    // Everything between the command letter and the colon is a PATH
    // ("S/TEMPORARY/:*" scratches inside TEMPORARY), and it used to be
    // discarded - the pattern was then matched against the CURRENT directory,
    // so C64 OS clearing its temp directory instead matched os/BOOTER. Nothing
    // was lost only because MFile::remove() refuses a container-hosted entry;
    // on a writable image it would have deleted the wrong files.
    std::string dirPath = command.substr(1, colon_position - 1);
    std::string pattern = command.substr(colon_position + 1);

    // drive number prefix ("S0:...")
    while ( !dirPath.empty() && isdigit((unsigned char) dirPath.front()) )
        dirPath.erase(0, 1);

    // As in open(): one leading '/' descends from the current directory, "//"
    // starts at the root of the image.
    if ( dirPath.size() > 1 && dirPath[0] == '/' && dirPath[1] != '/' )
        dirPath.erase(0, 1);
    while ( !dirPath.empty() && dirPath.back() == '/' )
        dirPath.pop_back();

    std::unique_ptr<MFile> dir(dirPath.empty()
                               ? MFSOwner::File(m_host.dosCwd()->fullUrl())
                               : m_host.dosCwd()->cd(dirPath));
    if ( dir != nullptr && dir->isDirectory() )
    {
        std::unique_ptr<MFile> entry;
        while ( (entry = std::unique_ptr<MFile>(dir->getNextFileInDir())) != nullptr )
        {
            if ( !entry->isDirectory() && isMatch(mstr::toPETSCII2(entry->name), pattern) )
            {
                Debug_printv("DELETING %s", entry->name.c_str());
                if ( entry->remove() ) n++;
            }
        }
    }

    m_host.dosSetStatus(ST_SCRATCHED, n);
}

// V: rebuild the BAM from the directory (D64MStream::validate).
void CbmDos::cmdValidate()
{
    Debug_printv("validate");
    std::unique_ptr<MFile> image;
    auto stream = openImageForUpdate(image);
    if ( stream == nullptr )
        return;

    uint8_t st = stream->validate();
    stream->close();
    if ( st == ST_OK )
        invalidateListings();
    m_host.dosSetStatus(st);
}


// --- U-x: user commands ------------------------------------------------------

bool CbmDos::cmdUser(const std::string& command)
{
    if ( command.size() < 2 )
        return false;

    char u = command[1];

    // 1581 U-R / U-W (shifted R/W) are U1/U2 (vdrive_command_u1a2b)
    if ( u == '-' && command.size() > 2 )
    {
        if ( (uint8_t) command[2] == 0xD2 ) u = '1';
        else if ( (uint8_t) command[2] == 0xD7 ) u = '2';
        else
        {
            m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
            return true;
        }
    }

    switch ( u )
    {
        // U1/UA read, U2/UB write: the whole block, no count byte.
        case '1': case 'A': cmdUserBlock(command, false); break;
        case '2': case 'B': cmdUserBlock(command, true);  break;

        // U3-U8 / UC-UH: jump to $0500 + 3*n in drive RAM.
        case '3': case '4': case '5': case '6': case '7': case '8':
        case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
        {
            uint8_t n = (u <= '8') ? (u - '3') : (u - 'C');
            m_host.dosMemory().execute(0x0500 + n * 3);
            break;
        }

        // U9 / UI / U;: UI+ and UI- set the bus speed; anything else is a warm
        // reset - channels closed, DOS version answered.
        case '9': case 'I': case ';':
            if ( command.size() == 3 && (command[2] == '+' || command[2] == '-') )
            {
                Debug_printv("%s bus speed", command[2] == '+' ? "C64" : "VIC-20");
                m_host.dosSetStatus(ST_OK);
                break;
            }
            Debug_printv("warm reset");
            m_host.dosReset();
            break;

        // UJ / U:: cold reset. A real drive takes a second to come back and
        // answers the DOS version.
        case 'J': case ':':
            Debug_printv("cold reset");
            m_host.dosReset();
            break;

        // U0 alone is accepted; the U0> burst/device-number forms are not
        // implemented (VICE answers 74 for them too).
        case '0':
            m_host.dosSetStatus(command.size() == 2 ? ST_OK : ST_DRIVE_NOT_READY);
            break;

        default:
            m_host.dosSetStatus(ST_DRIVE_NOT_READY);
            break;
    }
    return true;
}

// U1/U2 <channel> <drive> <track> <sector>
void CbmDos::cmdUserBlock(const std::string& command, bool for_write)
{
    int p[4] = { 0, 0, 0, 0 };
    if ( !blockParams(command, p, 4) || !trackSectorInRange(p[2], p[3]) )
        return;
    Debug_printv("U%c channel[%d] drive[%d] track[%d] sector[%d]",
                 for_write ? '2' : '1', p[0], p[1], p[2], p[3]);
    if ( m_host.dosBlockTransfer( p[0], p[2], p[3], for_write, false ) && for_write )
        invalidateListings();
}











// --- Helpers ----------------------------------------------------------------

bool CbmDos::blockParams(const std::string& command, int p[4], int needed)
{
    int n = parseBlockParams(command, p);
    if ( n < needed )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return false;
    }
    return true;
}

bool CbmDos::trackSectorInRange(int track, int sector)
{
    if ( track < 0 || track > 255 || sector < 0 || sector > 255 )
    {
        m_host.dosSetStatus(ST_ILLEGAL_TS_COMMAND, track & 0xFF, sector & 0xFF);
        return false;
    }
    return true;
}

std::shared_ptr<MStream> CbmDos::openImageForUpdate(std::unique_ptr<MFile>& image)
{
    MFile *cwd = m_host.dosCwd();
    if ( cwd == nullptr )
    {
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
        return nullptr;
    }

    // The container, never ImageBroker's stream (read-only), and never plain
    // out (that truncates the image).
    image.reset(MFSOwner::File(cwd->url));
    if ( image == nullptr )
    {
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
        return nullptr;
    }
    if ( !image->isWritable )
    {
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
        return nullptr;
    }
    auto stream = image->getSourceStream(std::ios_base::in | std::ios_base::out);
    // Only block-addressable media have a BAM to work on.
    if ( stream == nullptr || !stream->isOpen() || stream->sectorByteOffset(1, 0) < 0 )
    {
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
        return nullptr;
    }
    return stream;
}

void CbmDos::invalidateListings()
{
    // The cached listing stream has its own handle on the container and would
    // keep serving what was there before the write.
    MFile *cwd = m_host.dosCwd();
    if ( cwd != nullptr )
        ImageBroker::disposeUnderPath(cwd->url);
}

std::string CbmDos::stripDrivePrefix(const std::string& name)
{
    if ( name.size() >= 2 && isdigit((unsigned char) name[0]) && name[1] == ':' )
        return name.substr(2);
    if ( !name.empty() && name[0] == ':' )
        return name.substr(1);
    return name;
}

bool CbmDos::hasWildcard(const std::string& name)
{
    return name.find_first_of("*?") != std::string::npos;
}


// --- R, R-H, C ----------------------------------------------------------------

// R[d]:new=[d:]old (vdrive_command_rename): no wildcards on either side, the
// new name must be free (63), the old one must exist (62).
void CbmDos::cmdRename(const std::string& arg)
{
    std::string body = mstr::toUTF8(arg);
    size_t eq = body.find('=');
    if ( eq == std::string::npos )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }
    std::string dest = stripDrivePrefix(body.substr(0, eq));
    std::string src  = stripDrivePrefix(body.substr(eq + 1));
    if ( dest.empty() || src.empty() )
    {
        m_host.dosSetStatus(ST_SYNTAX_NO_NAME);
        return;
    }
    if ( hasWildcard(dest) || hasWildcard(src) )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }

    MFile *cwd = m_host.dosCwd();
    std::unique_ptr<MFile> existing(cwd->cd(dest));
    if ( existing != nullptr && existing->exists() )
    {
        m_host.dosSetStatus(ST_FILE_EXISTS);
        return;
    }
    std::unique_ptr<MFile> f(cwd->cd(src));
    if ( f == nullptr || !f->exists() )
    {
        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
        return;
    }
    if ( !f->isWritable )
    {
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
        return;
    }
    if ( !f->rename(dest) )
    {
        m_host.dosSetStatus(ST_WRITE_VERIFY);
        return;
    }
    Debug_printv("Renamed [%s] to [%s]", src.c_str(), dest.c_str());
    m_host.dosSetStatus(ST_OK);
}

// R-H:name (vdrive_command_renameheader)
void CbmDos::cmdRenameHeader(const std::string& arg)
{
    std::string name = stripDrivePrefix(mstr::toUTF8(arg));
    if ( hasWildcard(name) )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }
    MFile *cwd = m_host.dosCwd();
    std::unique_ptr<MFile> image(MFSOwner::File(cwd->url));
    if ( image == nullptr )
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
    else if ( !image->isWritable )
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
    else if ( !image->setDiskName(name) )
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);   // no header on this media
    else
        m_host.dosSetStatus(ST_OK);
}

// C[d]:new=[d:]old[,[d:]old2...] (vdrive_command_copy): every source must
// exist (62) before the destination is checked (63); the new file takes the
// first source's type and the rest are appended. REL is not concatenated.
void CbmDos::cmdCopy(const std::string& arg)
{
    std::string body = mstr::toUTF8(arg);
    size_t eq = body.find('=');
    if ( eq == std::string::npos )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }
    std::string dest = stripDrivePrefix(body.substr(0, eq));
    if ( dest.empty() )
    {
        m_host.dosSetStatus(ST_SYNTAX_NO_NAME);
        return;
    }
    if ( hasWildcard(dest) )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }

    MFile *cwd = m_host.dosCwd();
    std::vector<std::unique_ptr<MFile>> sources;
    for ( auto &part : mstr::split(body.substr(eq + 1), ',') )
    {
        std::string name = stripDrivePrefix(part);
        if ( name.empty() )
            continue;
        if ( sources.size() == 5 )   // CMD: up to five files
        {
            m_host.dosSetStatus(ST_SYNTAX_LONG_LINE);
            return;
        }
        std::unique_ptr<MFile> f(cwd->cd(name));
        if ( f == nullptr || !f->exists() )
        {
            m_host.dosSetStatus(ST_FILE_NOT_FOUND);
            return;
        }
        if ( f->isDirectory() )
        {
            m_host.dosSetStatus(ST_FILE_TYPE_MISMATCH);
            return;
        }
        sources.push_back(std::move(f));
    }
    if ( sources.empty() )
    {
        m_host.dosSetStatus(ST_SYNTAX_NO_NAME);
        return;
    }

    std::unique_ptr<MFile> out(cwd->cd(dest));
    if ( out == nullptr )
    {
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
        return;
    }
    if ( out->exists() )
    {
        m_host.dosSetStatus(ST_FILE_EXISTS);
        return;
    }
    if ( !out->isWritable )
    {
        m_host.dosSetStatus(ST_WRITE_PROTECT_ON);
        return;
    }

    // The new file takes the first source's type, read off its directory
    // entry: an MFile resolved by name carries none of its own. Media without
    // CBM types copy as PRG. REL is not concatenated (64).
    uint8_t type = 2;
    std::vector<std::shared_ptr<MStream>> inputs;
    for ( auto &src : sources )
    {
        auto is = src->getSourceStream(std::ios_base::in);
        if ( is == nullptr || !is->isOpen() )
        {
            m_host.dosSetStatus(ST_READ_NO_DATA);
            return;
        }
        int t = is->cbmFileType();
        if ( t == 4 || is->relRecordLength() != 0 )
        {
            m_host.dosSetStatus(ST_FILE_TYPE_MISMATCH);
            return;
        }
        if ( inputs.empty() && t >= 1 && t <= 3 )
            type = (uint8_t) t;
        inputs.push_back(is);
    }

    auto os = out->getSourceStream(std::ios_base::out);
    if ( os == nullptr || !os->isOpen() )
    {
        m_host.dosSetStatus(ST_WRITE_VERIFY);
        return;
    }
    os->setWriteFileType(type);

    uint8_t buf[256];
    uint8_t st = ST_OK;
    for ( auto &is : inputs )
    {
        uint32_t n;
        while ( (n = is->read(buf, sizeof(buf))) > 0 )
        {
            if ( os->write(buf, n) != n )
            {
                st = os->error() ? os->error() : ST_WRITE_VERIFY;
                break;
            }
        }
        is->close();
        if ( st != ST_OK )
            break;
    }
    os->close();
    if ( st == ST_OK && os->error() )
        st = os->error();

    invalidateListings();
    m_host.dosSetStatus(st);
}


// --- / (1581 sub-partitions) ------------------------------------------------

// /[d:]name selects a 1581 CBM sub-partition; "/" alone (or "/0:") goes back to
// the root, answering 02 PARTITION SELECTED (vdrive_command_chpart). The create
// form "/0:name,<t><s><lo><hi>,C" is not implemented.
void CbmDos::cmdSelectSubPartition(const std::string& command, size_t colon)
{
    std::string name = colon ? command.substr(colon + 1) : command.substr(1);
    if ( name.find(",C") != std::string::npos && name.size() >= 7 )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }
    name = mstr::toUTF8(name);

    if ( name.empty() )
    {
        // "//" is the root of the image (cdLocalRoot)
        m_host.dosSetCwd("//");
        if ( m_host.dosStatusCode() == ST_OK )
            m_host.dosSetStatus(ST_PARTITION_SELECTED, 1, 80);
        return;
    }

    std::unique_ptr<MFile> f(m_host.dosCwd()->cd(name));
    if ( f == nullptr || !f->exists() || !f->isDirectory() )
    {
        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
        return;
    }
    // The track range, as vdrive_command_chpart answers it (02,...,50,59)
    uint8_t first = 0, last = 0;
    {
        std::unique_ptr<MFile> image(MFSOwner::File(m_host.dosCwd()->url));
        auto stream = image ? image->getSourceStream() : nullptr;
        if ( stream == nullptr || !stream->subPartitionTracks(name, &first, &last) )
            first = last = 0;
    }
    m_host.dosSetCwd(name);
    if ( m_host.dosStatusCode() == ST_OK )
        m_host.dosSetStatus(ST_PARTITION_SELECTED, first, last);
}



#endif // BUILD_IEC
