#if defined(BUILD_IEC)

#include "cmd_dos.h"

#include <cstring>
#include <vector>

#include "../../drive.h"
#include "../../../../meatloaf/media/hd/dhd.h"

#include "../../../../../include/debug.h"
#include "../../../../../include/cbm_defines.h"

bool CmdHdDos::cmdChangePartition(const std::string& command)
{
    return partitionFromCommand(command);
}

std::string CmdHdDos::container()
{
    MFile *cwd = m_host.dosCwd();
    return cwd != nullptr ? DHDImageRegistry::containerOf(cwd->url) : std::string();
}

// G-P[<partition>] (vdrive_command_getpartinfo): 30 bytes plus a CR on the
// status channel. No partition byte, or 255, means the selected partition.
void CmdHdDos::cmdGetPartitionInfo()
{
    uint8_t len = m_rawLen;
    if ( m_raw != nullptr && len > 0 && m_raw[len - 1] == 0x0D )
        len--;
    if ( m_raw == nullptr || len < 3 || len > 4 )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }

    std::string url = container();
    DHDImageRegistry::Image *img = DHDImageRegistry::obtain(url);
    if ( img == nullptr )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }

    uint8_t part = (len == 4) ? m_raw[3] : img->selected;
    if ( part == 255 )
        part = img->selected;

    uint8_t reply[31];
    memset(reply, 0, sizeof(reply));
    reply[2] = part;

    uint8_t e[32];
    if ( part <= img->maxpart && DHDImageRegistry::readEntry(url, part, e) )
    {
        reply[0] = e[2];                    // type
        memcpy(&reply[3], &e[5], 16);       // name, $A0 padded
        reply[19] = e[0x15];                // start LBA
        reply[20] = e[0x16];
        reply[21] = e[0x17];
        reply[27] = e[0x1D];                // size in LBAs
        reply[28] = e[0x1E];
        reply[29] = e[0x1F];
    }
    if ( img->fd_kind )
        reply[1] = 0x80 | 0x40 | 0x20 | img->fd_kind;
    reply[30] = 0x0D;

    m_host.dosSetStatusData((const char *) reply, sizeof(reply));
}

// R-P:new=old (vdrive_command_renamepart): no wildcards, the new name must be
// free (63), the old one must exist (62).
void CmdHdDos::cmdRenamePartition(const std::string& arg)
{
    std::string body = mstr::toUTF8(arg);
    size_t eq = body.find('=');
    if ( eq == std::string::npos )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }
    std::string dest = body.substr(0, eq), src = body.substr(eq + 1);
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

    std::string url = container();
    DHDImageRegistry::Image *img = DHDImageRegistry::obtain(url);
    if ( img == nullptr )
    {
        m_host.dosSetStatus(ST_DRIVE_NOT_READY);
        return;
    }
    if ( img->byName(dest) != nullptr )
    {
        m_host.dosSetStatus(ST_FILE_EXISTS);
        return;
    }
    const DHDPartition *p = img->byName(src);
    if ( p == nullptr || p->number == 0 )
    {
        m_host.dosSetStatus(ST_FILE_NOT_FOUND);
        return;
    }
    if ( !DHDImageRegistry::renamePartition(url, p->number, mstr::toPETSCII2(dest)) )
    {
        m_host.dosSetStatus(ST_WRITE_VERIFY);
        return;
    }
    invalidateListings();
    m_host.dosSetStatus(ST_OK);
}

// D-P <count> <partition...> [$FF] (vdrive_command_deletepart): CMD FD only.
void CmdHdDos::cmdDeletePartition()
{
    uint8_t len = m_rawLen;
    if ( m_raw != nullptr && len > 0 && m_raw[len - 1] == 0x0D )
        len--;
    if ( m_raw == nullptr || len < 5 )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }
    uint8_t count = m_raw[3];
    // the list, optionally followed by the $FF VICE's layout carries
    if ( !(len == 4 + count || (len == 5 + count && m_raw[len - 1] == 0xFF)) || count == 0 )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }
    std::vector<uint8_t> numbers(m_raw + 4, m_raw + 4 + count);

    uint8_t st = DHDImageRegistry::deletePartitions(container(), numbers);
    invalidateListings();
    m_host.dosSetStatus(st);
}

#endif
