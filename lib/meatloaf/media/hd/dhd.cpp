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

#include "dhd.h"

#include "meat_media.h"
#include <cstring>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <climits>

std::map<std::string, DHDImageRegistry::Image> DHDImageRegistry::s_images;
bool DHDImageRegistry::s_probing = false;

const DHDPartition* DHDImageRegistry::Image::byNumber(uint8_t number) const
{
    for (auto &p : parts)
    {
        if (p.number == number)
            return &p;
    }
    return nullptr;
}

const DHDPartition* DHDImageRegistry::Image::byName(std::string utf8name) const
{
    bool wildcard = (mstr::contains(utf8name, "*") || mstr::contains(utf8name, "?"));
    for (auto &p : parts)
    {
        std::string pn = mstr::toUTF8(p.name);
        if (mstr::compareFilename(pn, utf8name, wildcard))
            return &p;
    }
    return nullptr;
}

std::string DHDImageRegistry::containerOf(const std::string &path)
{
    // The container path ends with a CMD media image component:
    // .dhd (HD), .d1m/.d2m/.d4m (FD-2000/FD-4000)
    static const char *exts[] = { ".dhd", ".d1m", ".d2m", ".d4m" };

    std::string lower = path;
    for (auto &c : lower)
        c = tolower(c);

    for (auto ext : exts)
    {
        size_t elen = strlen(ext);
        size_t p = lower.find(ext);
        while (p != std::string::npos)
        {
            size_t end = p + elen;
            if (end == lower.size() || lower[end] == '/')
                return path.substr(0, end);
            p = lower.find(ext, p + 1);
        }
    }
    return "";
}

DHDImageRegistry::Image* DHDImageRegistry::obtain(const std::string &containerUrl)
{
    if (containerUrl.empty())
        return nullptr;

    auto it = s_images.find(containerUrl);
    if (it != s_images.end() && it->second.valid)
        return &it->second;

    // (Re-)parse: not yet seen, or the image wasn't readable last time
    Image img;
    if (!parse(containerUrl, img))
        return nullptr;

    s_images[containerUrl] = std::move(img);
    return &s_images[containerUrl];
}

bool DHDImageRegistry::parse(const std::string &containerUrl, Image &img)
{
    // CMD HD boot code signature found at track 0, sector 5, offset $F0
    // of the system partition
    static const uint8_t hdmagic[16] = {
        0x43, 0x4D, 0x44, 0x20, 0x48, 0x44, 0x20, 0x20,   // "CMD HD  "
        0x8D, 0x03, 0x88, 0x8E, 0x02, 0x88, 0xEA, 0x60
    };
    // CMD FD series signature (same location) for D1M/D2M/D4M images
    static const uint8_t fdmagic[16] = {
        0x43, 0x4D, 0x44, 0x20, 0x46, 0x44, 0x20, 0x53,   // "CMD FD SERIES   "
        0x45, 0x52, 0x49, 0x45, 0x53, 0x20, 0x20, 0x20
    };

    // FD images have the system partition at a fixed location; on track 1
    // of it (track 0 is 8 sectors: offset = (track << 3) + sector) sits the
    // partition table, i.e. at sys_base + 2048. HD images are scanned and
    // use 256-sector tracks (table at sys_base + 65536).
    std::string lower = containerUrl;
    for (auto &c : lower)
        c = tolower(c);
    uint32_t fd_sys = 0;
    if (mstr::endsWith(lower, ".d1m"))
        fd_sys = 0x640 * 512;
    else if (mstr::endsWith(lower, ".d2m"))
        fd_sys = 0xC80 * 512;
    else if (mstr::endsWith(lower, ".d4m"))
        fd_sys = 0x1900 * 512;

    // Open the raw image bytes: the probing flag makes the CMD media
    // filesystems decline the path so the underlying filesystem serves it
    s_probing = true;
    std::unique_ptr<MFile> f(MFSOwner::File(containerUrl));
    std::shared_ptr<MStream> s = (f != nullptr) ? f->getSourceStream() : nullptr;
    s_probing = false;

    if (s == nullptr || !s->isOpen())
    {
        Debug_printv("Cannot open CMD media image [%s]", containerUrl.c_str());
        return false;
    }

    uint32_t image_size = s->size();
    uint8_t cfg[256];

    uint32_t sys_base = 0xFFFFFFFF;
    uint32_t table_base = 0;
    // A CMD HD holds a MAXIMUM OF 254 partitions. The loop below runs
    // i <= maxpart over the table's PHYSICAL entries: entry 0 is the system
    // partition (it supplies disk_label and is listed but never selectable),
    // and entries 1..254 are the user partitions. There is no entry 255.
    //
    // This agrees with the vendored VICE code, which is the reference to trust
    // here: lib/vdrive/vdrive.c:1180 caps at 254 with the comment "CMD HDs can
    // access 254 partitions (255 is system)", and vdrive.c:1201 remaps physical
    // entry 0 onto LOGICAL slot 255 (`k = (i == 0) ? 255 : i`) - so VICE's
    // "255" is the system partition under a different numbering, NOT a 255th
    // user partition. Raising this to 255 reads 32 bytes past the real table
    // and can fabricate a phantom partition from whatever follows.
    //
    // Both this and the loop counter must stay uint16_t: as uint8_t, an
    // "i <= 255" bound would never be false.
    uint16_t maxpart = 254;

    if (fd_sys != 0)
    {
        // CMD FD (D1M/D2M/D4M): fixed system partition location
        if (fd_sys + 0x600 <= image_size &&
            s->seek(fd_sys + 0x500) && s->read(cfg, sizeof(cfg)) == sizeof(cfg) &&
            memcmp(&cfg[0xF0], fdmagic, sizeof(fdmagic)) == 0)
        {
            sys_base = fd_sys;
            table_base = sys_base + 2048;
            maxpart = 31;
        }
    }
    else
    {
        // CMD HD: the system partition sits on a 64 KiB boundary
        for (uint32_t base = 0; base + 0x600 <= image_size; base += 65536)
        {
            if (!s->seek(base + 0x500))
                break;
            if (s->read(cfg, sizeof(cfg)) != sizeof(cfg))
                break;
            if (memcmp(&cfg[0xF0], hdmagic, sizeof(hdmagic)) == 0)
            {
                sys_base = base;
                table_base = sys_base + 65536;
                break;
            }
        }
    }

    if (sys_base == 0xFFFFFFFF)
    {
        Debug_printv("No CMD system partition found [%s]", containerUrl.c_str());
        return false;
    }

    img.default_part = cfg[0xE2];
    img.table_base = table_base;
    img.maxpart = maxpart;
    img.fd_kind = fd_sys == 0 ? 0 : (fd_sys == 0x640 * 512 ? 1 : (fd_sys == 0xC80 * 512 ? 2 : 4));

    // Partition table on track 1 of the system partition: 32-byte entries,
    // 8 per 256-byte sector, laid out contiguously. Entry 0 is the system
    // partition itself; entries 1..254 are the selectable partitions (CMD FD
    // caps at 31).
    uint8_t buf[32];
    for (uint16_t i = 0; i <= maxpart; i++)
    {
        uint32_t off = table_base + (uint32_t)i * 32;
        if (off + 32 > image_size)
            break;
        if (!s->seek(off))
            break;
        if (s->read(buf, sizeof(buf)) != sizeof(buf))
            break;

        std::string name = std::string((char *)&buf[5], 16);
        size_t e = name.find((char)0xA0);
        if (e != std::string::npos)
            name.resize(e);

        uint8_t type = buf[2];

        if (i == 0)
        {
            // Entry 0 is the system partition: it carries the drive label and
            // the partition table itself. Its type byte is 0xFF in every real
            // image (which is what vdrive's `ptype[part] == 255` test keys on),
            // so it would never pass the 1..4 filter below - it is added
            // explicitly instead, because it belongs in the partition listing.
            // It is NOT selectable: select() refuses partition 0, since the
            // system partition is not a mountable disk.
            img.disk_label = mstr::toUTF8(name);
        }
        else if (type < 1 || type > 4)
        {
            continue;
        }

        DHDPartition p;
        p.number = i;
        p.type = type;
        p.name = name;
        // 3-byte big-endian offset/size in 512-byte LBA blocks
        p.start = (((uint32_t)buf[0x15] << 16) | ((uint32_t)buf[0x16] << 8) | buf[0x17]) * 512;
        p.size = (((uint32_t)buf[0x1D] << 16) | ((uint32_t)buf[0x1E] << 8) | buf[0x1F]) * 512;
        img.parts.push_back(p);

        Debug_printv("partition[%d] type[%d] name[%s] start[%lu] size[%lu]",
                     p.number, p.type, p.name.c_str(), p.start, p.size);
    }

    // parts[] now always holds the system partition (entry 0), so "is it empty"
    // no longer answers "is anything mountable here" - count USER partitions.
    uint8_t first_user = 0;
    for (const DHDPartition &p : img.parts)
    {
        if (p.number != 0) { first_user = p.number; break; }
    }

    if (first_user == 0)
    {
        Debug_printv("No usable partitions in [%s]", containerUrl.c_str());
        return false;
    }

    // First use: select the default partition, falling back to the first USER
    // partition - never entry 0. parts[0] is the system partition now, so using
    // it as the fallback would mount the partition table as if it were a disk.
    img.selected = (img.default_part != 0 && img.byNumber(img.default_part))
                 ? img.default_part
                 : first_user;
    img.valid = true;

    Debug_printv("CMD %s [%s] label[%s] partitions[%d] selected[%d]",
                 fd_sys ? "FD" : "HD", containerUrl.c_str(),
                 img.disk_label.c_str(), img.parts.size(), img.selected);
    return true;
}

bool DHDImageRegistry::select(const std::string &containerUrl, uint8_t number)
{
    Image* img = obtain(containerUrl);
    if (img == nullptr)
        return false;

    // Partition 0 is the system partition. It appears in the listing (it is a
    // real table entry, and users want to see it), but it is not a mountable
    // disk - it holds the partition table. Refusing it HERE rather than in each
    // caller covers every route in: CP<n>, the console command's numeric form,
    // and a by-NAME lookup, since byNumber(0)/byName("system") now resolve.
    if (number == 0)
        return false;

    const DHDPartition* p = img->byNumber(number);
    if (p == nullptr)
        return false;

    if (img->selected == number)
        return true;

    img->selected = number;
    Debug_printv("selected partition[%d] type[%d] name[%s]", p->number, p->type, p->name.c_str());

    // Drop the broker-cached image stream so the next access decodes the
    // newly selected partition. cached_part records what IS cached, so after
    // discarding it that is nothing - not the partition we are about to use.
    // getDecodedStream() sets it when a stream is actually built.
    disposeCachedStream(containerUrl);
    img->cached_part = 0;

    return true;
}

std::shared_ptr<MStream> DHDImageRegistry::openRaw(const std::string &containerUrl, std::ios_base::openmode mode)
{
    // As parse(): the probing flag makes the CMD media filesystems decline the
    // path, so the underlying filesystem serves the raw image bytes.
    s_probing = true;
    std::unique_ptr<MFile> f(MFSOwner::File(containerUrl));
    std::shared_ptr<MStream> s = (f != nullptr) ? f->getSourceStream(mode) : nullptr;
    s_probing = false;
    if (s == nullptr || !s->isOpen())
        return nullptr;
    return s;
}

void DHDImageRegistry::forget(const std::string &containerUrl)
{
    s_images.erase(containerUrl);
    disposeCachedStream(containerUrl);
}

bool DHDImageRegistry::readEntry(const std::string &containerUrl, uint8_t number, uint8_t out[32])
{
    Image *img = obtain(containerUrl);
    if (img == nullptr || number > img->maxpart)
        return false;
    auto s = openRaw(containerUrl, std::ios_base::in);
    if (s == nullptr)
        return false;
    uint32_t off = img->table_base + (uint32_t) number * 32;
    return s->seek(off) && s->read(out, 32) == 32;
}

bool DHDImageRegistry::renamePartition(const std::string &containerUrl, uint8_t number, const std::string &petscii_name)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr || number == 0 || number > img->maxpart)
        return false;

    uint8_t name[16];
    memset(name, 0xA0, sizeof(name));
    memcpy(name, petscii_name.data(), std::min(petscii_name.size(), sizeof(name)));

    auto s = openRaw(containerUrl, std::ios_base::in | std::ios_base::out);
    if (s == nullptr)
        return false;
    uint32_t off = img->table_base + (uint32_t) number * 32 + 5;
    bool ok = s->seek(off) && s->write(name, sizeof(name)) == sizeof(name);
    s->close();

    // Keep the selection across the re-read.
    uint8_t selected = img->selected;
    forget(containerUrl);
    if (ok && (img = obtain(containerUrl)) != nullptr && img->byNumber(selected))
        img->selected = selected;
    return ok;
}

uint8_t DHDImageRegistry::deletePartitions(const std::string &containerUrl, const std::vector<uint8_t> &numbers)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr)
        return 74;
    // vdrive_command_deletepart: "can only do this on CMD FDs". On an FD the
    // system partition is at the END of the image, which is what makes
    // packing from LBA 0 safe; an HD's sits wherever it was formatted.
    if (img->fd_kind == 0)
        return 31;
    for (uint8_t n : numbers)
        if (n == 0 || n == 255 || n > img->maxpart)
            return 31;

    const uint16_t maxpart = img->maxpart;
    const uint32_t table_base = img->table_base;

    auto s = openRaw(containerUrl, std::ios_base::in | std::ios_base::out);
    if (s == nullptr)
        return 74;

    // The whole table, entries 0..maxpart
    std::vector<uint8_t> table((maxpart + 1) * 32);
    if (!s->seek(table_base) || s->read(table.data(), table.size()) != table.size())
        return 74;

    auto type   = [&](int i) -> uint8_t& { return table[i * 32 + 2]; };
    auto lba    = [&](int i, int o) -> uint32_t {
        return ((uint32_t) table[i * 32 + o] << 16) | ((uint32_t) table[i * 32 + o + 1] << 8) | table[i * 32 + o + 2];
    };
    auto setLba = [&](int i, int o, uint32_t v) {
        table[i * 32 + o] = (v >> 16) & 0xFF; table[i * 32 + o + 1] = (v >> 8) & 0xFF; table[i * 32 + o + 2] = v & 0xFF;
    };
    auto offset = [&](int i) { return (int64_t) lba(i, 0x15); };
    auto size   = [&](int i) { return (int64_t) lba(i, 0x1D); };

    for (uint8_t n : numbers)
        type(n) = 0;

    // vdrive_pack_parts: repeatedly move the physically nearest movable
    // partition (types 1-6) down against the previous one, never into a
    // foreign (type 7) partition. Offsets and sizes are in 512-byte LBAs.
    std::vector<uint8_t> buf(64 * 512);
    int64_t lowpos = -1, length = 1;
    for (int guard = 0; guard < 512; guard++)
    {
        int best = 0, block = 0;
        int64_t bestpos = INT64_MAX;
        for (int i = 1; i <= maxpart; i++)
        {
            if (type(i) > 0 && type(i) < 7 && offset(i) >= lowpos + length && offset(i) < bestpos)
            {
                for (int j = 1; j <= maxpart; j++)
                {
                    if (type(j) == 7 &&
                        ((offset(j) >= lowpos + length && offset(j) < lowpos + length + size(i)) ||
                         (offset(j) + size(j) >= lowpos + length && offset(j) + size(j) < lowpos + length + size(i))))
                    {
                        block = j;
                        break;
                    }
                }
                if (!block) { best = i; bestpos = offset(i); }
                else break;
            }
        }
        if (bestpos == INT64_MAX && !block)
            break;
        if (bestpos == INT64_MAX && block)
        {
            lowpos = offset(block);
            length = size(block);
            continue;
        }
        if (lowpos + length == bestpos)
        {
            lowpos = bestpos;
            length = size(best);
            continue;
        }

        // Move partition `best` from bestpos down to lowpos+length. The
        // destination is lower, so copying in ascending order never reads
        // a block this move has already overwritten.
        int64_t dest = lowpos + length;
        uint64_t bytes = (uint64_t) size(best) * 512;
        uint64_t src_b = (uint64_t) bestpos * 512, dst_b = (uint64_t) dest * 512;
        Debug_printv("D-P: moving partition[%d] from lba[%lld] to lba[%lld] size[%lld]",
                     best, (long long) bestpos, (long long) dest, (long long) size(best));
        for (uint64_t done = 0; done < bytes; )
        {
            uint32_t n = (uint32_t) std::min<uint64_t>(buf.size(), bytes - done);
            if (!s->seek(src_b + done) || s->read(buf.data(), n) != n)
                return 74;
            if (!s->seek(dst_b + done) || s->write(buf.data(), n) != n)
                return 25;
            done += n;
        }
        setLba(best, 0x15, (uint32_t) dest);
        lowpos = dest;
        length = size(best);
    }

    if (!s->seek(table_base) || s->write(table.data(), table.size()) != table.size())
        return 25;
    s->close();

    forget(containerUrl);
    return 0;
}

void DHDImageRegistry::disposeCachedStream(const std::string &containerUrl)
{
    // Key format mirrors ImageBroker::obtain(): type + the SOURCE file's url,
    // plus its pathInStream when it has one. The broker derives its key from a
    // freshly resolved MFile, so we must do the same to name the same entry.
    ImageBroker::disposeFor("d64", containerUrl);
}

const DHDPartition* DHDResolvePartition(const std::string &containerUrl,
                                        const std::string &in_path,
                                        std::string *out_rest,
                                        bool *out_explicit)
{
    if (out_rest) *out_rest = in_path;
    if (out_explicit) *out_explicit = false;

    DHDImageRegistry::Image *img = DHDImageRegistry::obtain(containerUrl);
    if (img == nullptr || !img->valid)
        return nullptr;

    const DHDPartition *p = nullptr;

    if (!in_path.empty())
    {
        std::string comp = in_path;
        size_t slash = comp.find('/');
        if (slash != std::string::npos)
            comp = comp.substr(0, slash);

        // A component made only of wildcard characters is the CBM "first
        // entry" pattern (LOAD"*"), never a deliberate partition reference.
        // byName() honours wildcards, so a bare "*" would otherwise match the
        // first partition and strip itself from the path, turning LOAD"*" into
        // a listing of that partition's root directory.
        bool wildcard_only = !comp.empty() &&
                             comp.find_first_not_of("*?") == std::string::npos;

        if (!comp.empty() && !wildcard_only)
        {
            bool numeric = comp.find_first_not_of("0123456789") == std::string::npos;
            if (numeric)
            {
                // Range-check before narrowing to uint8_t - an int silently
                // truncated into byNumber() is what once made "1571" resolve to
                // partition 35. v == 0 means "currently selected" and must NOT
                // go through byNumber(), which would return table entry 0, the
                // system partition.
                char *end = nullptr;
                long v = strtol(comp.c_str(), &end, 10);
                if (end != comp.c_str() && *end == '\0' && v >= 0 && v <= 254)
                    p = (v == 0) ? img->current() : img->byNumber((uint8_t)v);
            }
            else
            {
                p = img->byName(comp);
                // byName() honours wildcards, so a bare "*" can match the
                // system partition (entry 0). It is not mountable.
                if (p != nullptr && p->number == 0)
                    p = nullptr;
            }

            if (p != nullptr)
            {
                if (out_explicit) *out_explicit = true;
                if (out_rest)
                    *out_rest = (slash == std::string::npos) ? std::string()
                                                            : in_path.substr(slash + 1);
            }
        }
    }

    if (p == nullptr)
        p = img->current();

    return p;
}
