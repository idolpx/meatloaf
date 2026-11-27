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

// partition:create,<name>,<type|file>[,<tracks>][,@<n>][,#<slot>]
// partition:delete,<n>[,<n>...]
// partition:default,<n>
// partition:dump,<n>[,<filename>]
// partition:write,<n>,<file>
//
// (A new CMD container is made by N0: or MD: with a .dhd .d1m .d2m .d4m name.)
//
// Meatloaf extensions: on CMD hardware only HD-TOOLS and FD-TOOLS create
// partitions or change the default. <type> is nat, 1541, 1571 or 1581. The
// fixed types size themselves (683 / 1366 / 3200 blocks); a native partition
// needs <tracks>, 1-255, each 256 blocks (64 KB). Without @<n> the partition
// goes in the first free gap that fits; with it, where partition n starts, and
// every partition from there on moves up. #<slot> picks the table slot (the
// partition's number); without it, or with #0, the lowest free one is used -
// 63 if the slot is taken, 77 if it is beyond the table. The new partition is
// formatted and its number answered in the status's track field: 00, OK,<n>,00.
//
// partition:dump,<n>[,<filename>] writes partition n's bytes to a standalone
// image beside the container - .d64 / .d71 / .d81 / .dnp by its type, named
// after the partition unless a filename is given (63 if it exists).
// partition:dump,0 on a CMD HD saves the system partition - the drive's DOS -
// to /sd/.sys/cmdhd.sys, which N0:/MD: then use for new DHDs.
//
// A .d64/.d71/.d81/.dnp file in place of <type> creates a partition of that
// file's type and size and writes the file in instead of formatting;
// partition:write,<n>,<file> does the same to an existing partition (64 if
// the types differ, 52 if it does not fit). Files are relative to the
// directory the image is in unless absolute or a URL.
void CmdHdDos::cmdPartitionAdmin(const std::string& args)
{
    std::vector<std::string> a = mstr::split(args, ',');
    for ( auto &x : a )
        mstr::trim(x);
    if ( a.empty() )
    {
        m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
        return;
    }

    std::string url = container();
    if ( DHDImageRegistry::obtain(url) == nullptr )
    {
        m_host.dosSetStatus(ST_SYNTAX_INVALID);
        return;
    }

    auto number = [](const std::string& s, long lo, long hi, long* out) {
        char *end = nullptr;
        long v = strtol(s.c_str(), &end, 10);
        if ( s.empty() || *end != '\0' || v < lo || v > hi ) return false;
        *out = v;
        return true;
    };

    const std::string& verb = a[0];

    // partition:write,<n>,<file>: a D64/D71/D81/DNP into an existing partition
    if ( verb == "write" )
    {
        long n = 0;
        if ( a.size() != 3 || !number(a[1], 1, 254, &n) || a[2].empty() )
        {
            m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
            return;
        }
        uint8_t st = DHDImageRegistry::writePartitionImage(url, (uint8_t) n, mediaPath(a[2]));
        invalidateListings();
        m_host.dosSetStatus(st);
        return;
    }

    if ( verb == "default" )
    {
        long n = 0;
        if ( a.size() != 2 || !number(a[1], 1, 254, &n) )
        {
            m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
            return;
        }
        m_host.dosSetStatus(DHDImageRegistry::setDefaultPartition(url, (uint8_t) n));
        return;
    }

    if ( verb == "delete" )
    {
        std::vector<uint8_t> numbers;
        for ( size_t i = 1; i < a.size(); i++ )
        {
            long n = 0;
            if ( !number(a[i], 1, 254, &n) )
            {
                m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                return;
            }
            numbers.push_back((uint8_t) n);
        }
        if ( numbers.empty() )
        {
            m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
            return;
        }
        uint8_t st = DHDImageRegistry::deletePartitions(url, numbers);
        invalidateListings();
        m_host.dosSetStatus(st);
        return;
    }

    if ( verb == "dump" )
    {
        long n = 0;
        if ( a.size() < 2 || a.size() > 3 || !number(a[1], 0, 254, &n) )
        {
            m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
            return;
        }
        std::string name = a.size() == 3 ? a[2] : std::string();
        m_host.dosSetStatus(DHDImageRegistry::dumpPartition(url, (uint8_t) n, name));
        return;
    }

    if ( verb == "create" )
    {
        if ( a.size() < 3 || a[1].empty() || hasWildcard(a[1]) )
        {
            m_host.dosSetStatus(a.size() >= 2 && a[1].empty() ? ST_SYNTAX_NO_NAME : ST_SYNTAX_UNKNOWN);
            return;
        }
        const std::string& name = a[1];
        const std::string& kind = a[2];

        uint8_t type = 0;
        uint32_t lbas = 0;
        // A media file instead of a type: the partition takes the file's type
        // and size, and the file is written in rather than formatted.
        std::string media;
        if ( kind.find('.') != std::string::npos )
        {
            media = mediaPath(kind);
            uint8_t err = 0;
            if ( !DHDImageRegistry::mediaGeometry(media, &type, &lbas, &err) )
            {
                m_host.dosSetStatus(err);
                return;
            }
        }
        else if ( kind == "nat" || kind == "native" ) type = 1;
        else if ( kind == "1541" ) { type = 2; lbas = 342; }    // 683 blocks
        else if ( kind == "1571" ) { type = 3; lbas = 683; }    // 1366 blocks
        else if ( kind == "1581" ) { type = 4; lbas = 1600; }   // 3200 blocks
        else
        {
            m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
            return;
        }

        long tracks = 0, before = 0, slot_req = 0;
        for ( size_t i = 3; i < a.size(); i++ )
        {
            if ( !a[i].empty() && a[i][0] == '@' )
            {
                if ( !number(a[i].substr(1), 1, 254, &before) )
                {
                    m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                    return;
                }
            }
            else if ( !a[i].empty() && a[i][0] == '#' )
            {
                // the table slot; #0 (or none) takes the lowest free one
                if ( !number(a[i].substr(1), 0, 255, &slot_req) )
                {
                    m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                    return;
                }
                if ( slot_req == 255 )
                {
                    m_host.dosSetStatus(ST_PARTITION_ILLEGAL);
                    return;
                }
            }
            else if ( type != 1 || !media.empty() || !number(a[i], 1, 255, &tracks) )
            {
                // a size only means something for a native partition
                m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                return;
            }
        }
        if ( type == 1 && media.empty() )
        {
            if ( tracks == 0 )
            {
                m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
                return;
            }
            lbas = (uint32_t) tracks * 128;          // 256 blocks = 64 KB = 128 LBAs per track
        }

        bool had_default = DHDImageRegistry::obtain(url)->default_part != 0;
        uint8_t slot = 0;
        uint8_t st = DHDImageRegistry::createPartition(url, type, mstr::toPETSCII2(name), lbas, (uint8_t) before, (uint8_t) slot_req, &slot);
        if ( st != ST_OK )
        {
            m_host.dosSetStatus(st);
            return;
        }
        // A new image has no default; its first partition becomes it, as a
        // CMD-made image with one partition has E2 = 1.
        if ( !had_default )
            DHDImageRegistry::setDefaultPartition(url, slot);

        if ( !media.empty() )
        {
            // Write the media file in instead of formatting
            st = DHDImageRegistry::writePartitionImage(url, slot, media);
            invalidateListings();
            m_host.dosSetStatus(st, st == ST_OK ? slot : 0, 0);
            return;
        }

        // Format it: select it for the format, then put the selection back.
        // An image with nothing selected yet (a new one) keeps the new
        // partition selected.
        DHDImageRegistry::Image *img = DHDImageRegistry::obtain(url);
        uint8_t previous = img ? img->selected : 0;
        bool formatted = false;
        if ( DHDImageRegistry::select(url, slot) )
        {
            std::unique_ptr<MFile> part(MFSOwner::File(url));
            formatted = part && part->format(name + ",01");
        }
        if ( previous )
            DHDImageRegistry::select(url, previous);
        invalidateListings();

        Debug_printv("partition:create [%s] type[%d] lbas[%lu] -> %d formatted[%d]", name.c_str(), type, (unsigned long) lbas, slot, formatted);
        if ( !formatted )
            m_host.dosSetStatus(ST_WRITE_VERIFY, slot, 0);
        else
            m_host.dosSetStatus(ST_OK, slot, 0);
        return;
    }

    m_host.dosSetStatus(ST_SYNTAX_UNKNOWN);
}

#endif
