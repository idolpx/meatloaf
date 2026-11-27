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
#include <cerrno>
#include <unistd.h>

std::map<std::string, DHDImageRegistry::Image> DHDImageRegistry::s_images;

// Where partition:dump,0 keeps a CMD HD's system partition (its DOS) and
// N0:/MD: take it from for a new DHD.
#define SYSTEM_TEMPLATE "/sd/.sys/cmdhd.sys"
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
    img.sys_base = sys_base;
    img.image_size = image_size;
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

    // An image with no user partitions yet is valid - N0:/MD: make
    // exactly that, and partition:create needs to reach it - it just has
    // nothing selected (selected 0; current() answers nullptr for it).
    if (first_user == 0)
        Debug_printv("No partitions yet in [%s]", containerUrl.c_str());

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

// --- Partition table editing (D-P, partition:create, partition:default) -----

namespace {

// The raw partition table, entries 0..maxpart, with helpers over its fields.
// Offsets and sizes are 512-byte LBAs.
struct PartTable
{
    std::vector<uint8_t> raw;

    uint8_t& type(int i) { return raw[i * 32 + 2]; }
    uint32_t field(int i, int o) const
    {
        return ((uint32_t) raw[i * 32 + o] << 16) | ((uint32_t) raw[i * 32 + o + 1] << 8) | raw[i * 32 + o + 2];
    }
    void setField(int i, int o, uint32_t v)
    {
        raw[i * 32 + o] = (v >> 16) & 0xFF; raw[i * 32 + o + 1] = (v >> 8) & 0xFF; raw[i * 32 + o + 2] = v & 0xFF;
    }
    uint32_t start(int i) const { return field(i, 0x15); }
    uint32_t size(int i) const { return field(i, 0x1D); }
};

// Where partitions may live on this image, in LBAs: [first, end). On an FD
// that is everything before the system partition (which is at the end); on
// an HD everything after the system partition (the area before it is not for
// partitions either - the reference image starts its first one right after).
void partitionArea(const DHDImageRegistry::Image& img, PartTable& t, int64_t& first, int64_t& end)
{
    int64_t sys = img.sys_base / 512;
    if (img.fd_kind)
    {
        first = 0;
        end = sys;
    }
    else
    {
        first = sys + std::max<int64_t>(t.size(0), 1);
        end = img.image_size / 512;
    }
}

// Copy `lbas` LBAs from src to dst, whichever way they overlap: ascending
// when moving down, descending when moving up, so no block is read after the
// move has overwritten it.
bool moveLbas(std::shared_ptr<MStream>& s, uint64_t src, uint64_t dst, uint64_t lbas)
{
    if (src == dst || lbas == 0)
        return true;
    std::vector<uint8_t> buf(64 * 512);
    uint64_t bytes = lbas * 512, step = buf.size();
    bool up = dst > src;
    for (uint64_t done = 0; done < bytes; )
    {
        uint32_t n = (uint32_t) std::min<uint64_t>(step, bytes - done);
        uint64_t off = up ? bytes - done - n : done;
        if (!s->seek(src * 512 + off) || s->read(buf.data(), n) != n)
            return false;
        if (!s->seek(dst * 512 + off) || s->write(buf.data(), n) != n)
            return false;
        done += n;
    }
    return true;
}

} // namespace

bool DHDImageRegistry::loadTable(Image* img, std::shared_ptr<MStream>& s, std::vector<uint8_t>& raw)
{
    raw.assign((img->maxpart + 1) * 32, 0);
    return s->seek(img->table_base) && s->read(raw.data(), raw.size()) == raw.size();
}

bool DHDImageRegistry::saveTable(Image* img, std::shared_ptr<MStream>& s, const std::vector<uint8_t>& raw)
{
    return s->seek(img->table_base) && s->write(raw.data(), raw.size()) == raw.size();
}

uint8_t DHDImageRegistry::deletePartitions(const std::string &containerUrl, const std::vector<uint8_t> &numbers)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr)
        return 74;
    for (uint8_t n : numbers)
        if (n == 0 || n == 255 || n > img->maxpart)
            return 31;

    const int maxpart = img->maxpart;
    auto s = openRaw(containerUrl, std::ios_base::in | std::ios_base::out);
    if (s == nullptr)
        return 74;

    PartTable t;
    if (!loadTable(img, s, t.raw))
        return 74;

    // Deleting a partition that is not there is an error, not a no-op (VICE
    // only range-checks; this answers 77 before anything changes).
    for (uint8_t n : numbers)
        if (t.type(n) == 0)
            return 77;
    for (uint8_t n : numbers)
        t.type(n) = 0;

    int64_t area_first, area_end;
    partitionArea(*img, t, area_first, area_end);

    // vdrive_pack_parts: repeatedly move the physically nearest movable
    // partition (types 1-6) down against the previous one, never into a
    // foreign (type 7) partition. The pack starts at the beginning of the
    // partition area - LBA 0 on an FD, the end of the system partition on an
    // HD - which is what VICE's FD-only rule was protecting.
    auto offset = [&](int i) { return (int64_t) t.start(i); };
    auto size   = [&](int i) { return (int64_t) t.size(i); };
    // A partition that is not wholly inside the image cannot be moved - its
    // data is not all there to read - so it is treated as foreign: fixed, and
    // nothing is moved onto it. The data is moved before the table is
    // written, so a read failing half way would leave the two disagreeing.
    auto fixed = [&](int i) {
        return t.type(i) == 7 || offset(i) < area_first || offset(i) + size(i) > area_end;
    };
    int64_t lowpos = area_first - 1, length = 1;
    for (int guard = 0; guard < 512; guard++)
    {
        int best = 0, block = 0;
        int64_t bestpos = INT64_MAX;
        for (int i = 1; i <= maxpart; i++)
        {
            if (t.type(i) > 0 && !fixed(i) && offset(i) >= lowpos + length && offset(i) < bestpos)
            {
                for (int j = 1; j <= maxpart; j++)
                {
                    if (t.type(j) != 0 && fixed(j) &&
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

        int64_t dest = lowpos + length;
        Debug_printv("D-P: moving partition[%d] from lba[%lld] to lba[%lld] size[%lld]",
                     best, (long long) bestpos, (long long) dest, (long long) size(best));
        if (!moveLbas(s, bestpos, dest, size(best)))
            return 25;
        t.setField(best, 0x15, (uint32_t) dest);
        lowpos = dest;
        length = size(best);
    }

    if (!saveTable(img, s, t.raw))
        return 25;
    s->close();

    // A DHD shrinks back to fit: after the pack every free LBA is at the end,
    // so cut the file at the end of the last partition (never below the
    // system partition). A partition running past the end of the image keeps
    // the part of it that is there. Local files only - truncate() has no
    // equivalent over a network share, where the image keeps its size.
    if (!img->fd_kind && !containerUrl.empty() && containerUrl[0] == '/')
    {
        int64_t end = area_first;
        for (int i = 1; i <= maxpart; i++)
            if (t.type(i) != 0)
                end = std::max<int64_t>(end, std::min<int64_t>(offset(i) + size(i), area_end));
        if (end < area_end)
        {
            Debug_printv("D-P: shrinking [%s] from %lld to %lld LBAs", containerUrl.c_str(),
                         (long long) area_end, (long long) end);
            if (truncate(containerUrl.c_str(), (off_t) end * 512) != 0)
                Debug_printv("D-P: truncate failed errno[%d]", errno);
        }
    }

    forget(containerUrl);
    return 0;
}

uint8_t DHDImageRegistry::createPartition(const std::string &containerUrl, uint8_t type, const std::string &petscii_name,
                                          uint32_t lbas, uint8_t insert_before, uint8_t slot_req, uint8_t *out_number)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr)
        return 74;
    if (type < 1 || type > 4 || lbas == 0)
        return 30;

    const int maxpart = img->maxpart;
    auto s = openRaw(containerUrl, std::ios_base::in | std::ios_base::out);
    if (s == nullptr)
        return 74;

    PartTable t;
    if (!loadTable(img, s, t.raw))
        return 74;

    // Names are unique, as R-P keeps them
    uint8_t name[16];
    memset(name, 0xA0, sizeof(name));
    memcpy(name, petscii_name.data(), std::min(petscii_name.size(), sizeof(name)));
    for (int i = 1; i <= maxpart; i++)
        if (t.type(i) != 0 && memcmp(&t.raw[i * 32 + 5], name, 16) == 0)
            return 63;

    // The slot asked for, or the lowest free one. Numbering has nothing to do
    // with placement on the disk, and the table always holds every entry.
    int slot = 0;
    if (slot_req != 0)
    {
        if (slot_req > maxpart)
            return 77;
        if (t.type(slot_req) != 0)
            return 63;
        slot = slot_req;
    }
    for (int i = 1; i <= maxpart && !slot; i++)
        if (t.type(i) == 0)
            slot = i;
    if (!slot)
        return 72;

    int64_t area_first, area_end;
    partitionArea(*img, t, area_first, area_end);
    // An FD is its fixed size. A DHD grows to take a new partition - up to
    // 4 GB, the most a FAT32 file can be (the table's 3-byte LBA allows more).
    // area_end stays the image's current end: a partition past it is one that
    // cannot be moved (see deletePartitions()).
    const int64_t alloc_end = img->fd_kind ? area_end : (int64_t) (0xFFFFFFFFull / 512);

    // Everything that occupies the area, sorted by start
    std::vector<int> used;
    for (int i = 1; i <= maxpart; i++)
        if (t.type(i) != 0)
            used.push_back(i);
    std::sort(used.begin(), used.end(), [&](int a, int b) { return t.start(a) < t.start(b); });

    int64_t at = -1;
    if (insert_before == 0)
    {
        // First fit: the first gap at least `lbas` long
        int64_t cursor = area_first;
        for (int i : used)
        {
            int64_t st = t.start(i), en = st + t.size(i);
            if (en <= area_first || st >= alloc_end)
                continue;
            if (st - cursor >= (int64_t) lbas)
                break;
            cursor = std::max(cursor, en);
        }
        // No gap: after the last partition - on a DHD that grows the file
        if (cursor + (int64_t) lbas <= alloc_end)
            at = cursor;
        if (at < 0)
            return 72;
    }
    else
    {
        // Insert: take the place of `insert_before` and move it, and every
        // partition after it, up by `lbas`. A foreign partition (type 7) is
        // never moved, as in the pack, so it cannot be in the way.
        if (insert_before > maxpart || t.type(insert_before) == 0)
            return 77;
        at = t.start(insert_before);

        // Everything from `at` up to the next foreign partition (or the end of
        // the area) moves; it has to fit in front of whatever stops it. An
        // entry starting past the end of the image is ignored - it cannot be
        // moved, and the reference image has such entries.
        // A foreign partition, or one not wholly inside the image, stops the
        // move: those are never moved (see deletePartitions()).
        auto fixed = [&](int i) {
            return t.type(i) == 7 || (int64_t) t.start(i) < area_first ||
                   (int64_t) (t.start(i) + t.size(i)) > area_end;
        };
        if (fixed(insert_before))
            return 77;
        int64_t limit = alloc_end;
        for (int i : used)
            if (fixed(i) && (int64_t) t.start(i) > at)
            {
                limit = std::min<int64_t>(limit, t.start(i));
            }
        std::vector<int> moving;
        int64_t last_end = at;
        for (int i : used)
        {
            int64_t st = t.start(i);
            if (st >= at && st < limit && !fixed(i))
            {
                moving.push_back(i);
                last_end = std::max<int64_t>(last_end, st + t.size(i));
            }
        }
        if (last_end + (int64_t) lbas > limit)
            return 72;
        // From the top down, so no partition is moved onto one not yet moved
        for (auto it = moving.rbegin(); it != moving.rend(); ++it)
        {
            int i = *it;
            Debug_printv("partition:create: moving partition[%d] from lba[%lu] to lba[%lld]",
                         i, (unsigned long) t.start(i), (long long) (t.start(i) + lbas));
            if (!moveLbas(s, t.start(i), t.start(i) + lbas, t.size(i)))
                return 25;
            t.setField(i, 0x15, t.start(i) + lbas);
        }
    }

    // The new entry: keep bytes 0-1, which on the first entry of each table
    // sector are that sector's link.
    uint8_t *e = &t.raw[slot * 32];
    memset(e + 2, 0, 30);
    e[2] = type;
    memcpy(e + 5, name, 16);
    t.setField(slot, 0x15, (uint32_t) at);
    t.setField(slot, 0x1D, lbas);

    // Zero the new partition's area so the format starts from nothing
    std::vector<uint8_t> zero(64 * 512, 0);
    for (uint64_t done = 0; done < (uint64_t) lbas * 512; )
    {
        uint32_t n = (uint32_t) std::min<uint64_t>(zero.size(), (uint64_t) lbas * 512 - done);
        if (!s->seek((uint64_t) at * 512 + done) || s->write(zero.data(), n) != n)
            return 25;
        done += n;
    }

    if (!saveTable(img, s, t.raw))
        return 25;
    s->close();

    Debug_printv("partition:create: [%d] type[%d] at lba[%lld] size[%lu]", slot, type, (long long) at, (unsigned long) lbas);
    forget(containerUrl);
    if (out_number)
        *out_number = (uint8_t) slot;
    return 0;
}

uint8_t DHDImageRegistry::setDefaultPartition(const std::string &containerUrl, uint8_t number)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr)
        return 74;
    if (number == 0 || img->byNumber(number) == nullptr)
        return 77;
    auto s = openRaw(containerUrl, std::ios_base::in | std::ios_base::out);
    if (s == nullptr)
        return 74;
    // cfg sits at sys_base + $500; the default partition is its byte $E2,
    // and $E3 carries the same value on real images (05 05 on the reference
    // HD) and in VICE's format.
    uint8_t both[2] = { number, number };
    if (!s->seek(img->sys_base + 0x500 + 0xE2) || s->write(both, 2) != 2)
        return 25;
    s->close();
    img->default_part = number;
    return 0;
}

uint8_t DHDImageRegistry::dumpPartition(const std::string &containerUrl, uint8_t number, std::string filename)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr)
        return 74;
    // Partition 0 is the system partition. On an HD it carries the drive's
    // DOS, so it is worth keeping: by default it goes to SYSTEM_TEMPLATE,
    // which createImage() then uses for every new DHD. An FD's system
    // partition holds only the table and config (its DOS is in ROM), and its
    // entry 0 has no start or size, so there is nothing to dump.
    const DHDPartition *p = img->byNumber(number);
    if (p == nullptr || p->size == 0 || (number == 0 && img->fd_kind))
        return 77;

    std::string target;
    std::string ext;
    if (number == 0)
    {
        ext = ".sys";
        if (filename.empty())
        {
            target = SYSTEM_TEMPLATE;
            std::unique_ptr<MFile> dir(MFSOwner::File(target.substr(0, target.find_last_of('/')), true));
            if (dir && !dir->exists())
                dir->mkDir();
        }
    }
    else
    {
        static const char *exts[] = { "", ".dnp", ".d64", ".d71", ".d81" };
        if (p->type < 1 || p->type > 4)
            return 64;
        ext = exts[p->type];
    }

    if (target.empty() && filename.empty())
        filename = mstr::toUTF8(p->name);
    // a plain file name, never a path
    for (auto &c : filename)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?')
            c = '_';
    mstr::trim(filename);
    if (target.empty())
    {
        if (filename.empty())
            return 34;
        if (!mstr::endsWith(filename, ext.c_str(), false))
            filename += ext;

        size_t slash = containerUrl.find_last_of('/');
        std::string dir = slash == std::string::npos ? std::string() : containerUrl.substr(0, slash);
        target = dir + "/" + filename;
    }

    // Raw on both ends: the target's name says .d64, which would otherwise
    // resolve to an image decoder rather than the file.
    std::unique_ptr<MFile> out(MFSOwner::File(target, true));
    if (out == nullptr)
        return 74;
    if (out->exists())
        return 63;
    if (!out->isWritable)
        return 26;

    auto src = openRaw(containerUrl, std::ios_base::in);
    if (src == nullptr)
        return 74;
    auto dst = out->getSourceStream(std::ios_base::out);
    if (dst == nullptr || !dst->isOpen())
        return 25;

    // The image's own size, not the partition window's: a 1541 partition is
    // 342 LBAs = 175104 bytes, 256 more than a D64, and a file of that size is
    // not a valid D64 to other tools. The other windows are exact.
    uint32_t length = p->size;
    if (p->type == 2 && length > 174848)
        length = 174848;

    std::vector<uint8_t> buf(64 * 512);
    uint8_t st = 0;
    for (uint32_t done = 0; done < length; )
    {
        uint32_t n = std::min<uint32_t>((uint32_t) buf.size(), length - done);
        if (!src->seek(p->start + done) || src->read(buf.data(), n) != n)
        {
            st = 74;
            break;
        }
        if (dst->write(buf.data(), n) != n)
        {
            st = 25;
            break;
        }
        done += n;
    }
    dst->close();
    if (st == 0 && dst->error())
        st = 25;
    if (st != 0)
        out->remove();
    else
        Debug_printv("partition:dump [%d] -> [%s] (%lu bytes)", number, target.c_str(), (unsigned long) length);
    return st;
}

bool DHDImageRegistry::isContainerName(const std::string &name)
{
    std::string lower = name;
    for (auto &c : lower) c = tolower(c);
    return mstr::endsWith(lower, ".dhd") || mstr::endsWith(lower, ".d1m") ||
           mstr::endsWith(lower, ".d2m") || mstr::endsWith(lower, ".d4m");
}

uint8_t DHDImageRegistry::createImage(const std::string &url)
{
    std::string lower = url;
    for (auto &c : lower) c = tolower(c);

    uint32_t psize = 0, ssize = 0;              // FD: data LBAs, table-track sectors
    bool hd = false;
    if (mstr::endsWith(lower, ".d1m"))      { psize = 0x640;  ssize = 32; }
    else if (mstr::endsWith(lower, ".d2m")) { psize = 0xC80;  ssize = 72; }
    else if (mstr::endsWith(lower, ".d4m")) { psize = 0x1900; ssize = 152; }
    else if (mstr::endsWith(lower, ".dhd")) hd = true;
    else return 64;

    std::unique_ptr<MFile> f(MFSOwner::File(url, true));
    if (f == nullptr)
        return 74;
    if (f->exists())
        return 63;
    if (!f->isWritable)
        return 26;
    auto s = f->getSourceStream(std::ios_base::in | std::ios_base::out | std::ios_base::trunc);
    if (s == nullptr || !s->isOpen())
        return 25;

    // The system partition, built in memory: track 0 (config at sector 5) then
    // the table track. 256-byte sectors.
    const uint32_t track0 = hd ? 256 : 8;       // sectors in track 0
    const uint32_t tsecs = hd ? 32 : 4;         // table sectors
    const uint32_t sys_sectors = hd ? (track0 + 32) : (track0 + ssize);
    std::vector<uint8_t> sys(sys_sectors * 256, 0);
    uint8_t *table = &sys[track0 * 256];
    for (uint32_t k = 0; k < tsecs; k++)
    {
        table[k * 256 + 0] = (k + 1 < tsecs) ? 1 : 0;
        table[k * 256 + 1] = (k + 1 < tsecs) ? (uint8_t) (k + 1) : 0xFF;
    }
    table[2] = 0xFF;                            // entry 0: the system partition
    memset(&table[5], 0xA0, 16);
    memcpy(&table[5], "SYSTEM", 6);

    uint8_t *cfg = &sys[5 * 256];
    uint64_t sys_lba, total;
    bool from_template = false;
    if (hd)
    {
        // A system partition dumped from a real CMD HD (partition:dump,0)
        // carries the drive's DOS: use it when there is one, so the image
        // boots on hardware too. Its table is emptied below.
        std::unique_ptr<MFile> tf(MFSOwner::File(SYSTEM_TEMPLATE, true));
        auto ts = (tf && tf->exists()) ? tf->getSourceStream(std::ios_base::in) : nullptr;
        if (ts != nullptr && ts->size() == sys.size())
        {
            uint32_t got = 0;
            while (got < sys.size())
            {
                uint32_t r = ts->read(sys.data() + got, sys.size() - got);
                if (r == 0) break;
                got += r;
            }
            from_template = (got == sys.size());
            if (!from_template)
                memset(sys.data(), 0, sys.size());
        }
    }
    if (from_template)
    {
        // Keep the DOS and config; empty the table, keeping each sector's
        // link, and keep entry 0 (the system partition) itself.
        for (uint32_t k = 0; k < tsecs; k++)
            for (uint32_t e = 0; e < 8; e++)
            {
                if (k == 0 && e == 0)
                    continue;
                memset(&table[k * 256 + e * 32 + 2], 0, 30);
            }
    }
    if (hd)
    {
        static const uint8_t hdmagic[16] = {
            0x43, 0x4D, 0x44, 0x20, 0x48, 0x44, 0x20, 0x20, 0x8D, 0x03, 0x88, 0x8E, 0x02, 0x88, 0xEA, 0x60 };
        if (!from_template)
            memcpy(&cfg[0xF0], hdmagic, 16);
        else if (memcmp(&cfg[0xF0], hdmagic, 16) != 0)
            return 64;                          // not a CMD HD system partition
        sys_lba = 128;
        uint32_t sys_lbas = sys_sectors / 2;    // 144
        table[0x15] = (sys_lba >> 16) & 0xFF; table[0x16] = (sys_lba >> 8) & 0xFF; table[0x17] = sys_lba & 0xFF;
        table[0x1D] = (sys_lbas >> 16) & 0xFF; table[0x1E] = (sys_lbas >> 8) & 0xFF; table[0x1F] = sys_lbas & 0xFF;
        // Minimal: it ends with the system partition. partition:create grows it.
        total = (sys_lba + sys_lbas) * 512;
    }
    else
    {
        // The config sector as real CMD FD images have it (three checked: a
        // D1M, D2M and D4M). The data-area size is split: its high byte at
        // $71, its low byte at $A9. VICE's vdrive_command_format_internal
        // writes it little-endian at $70-$71 instead; its own detection never
        // reads these, so the difference never shows there.
        memset(cfg, 0xFF, 224);
        cfg[0x00] = 0; cfg[0x38] = 0; cfg[0x39] = 0;
        cfg[0x70] = 0; cfg[0x71] = (psize >> 8) & 0xFF;
        cfg[0xA8] = 0; cfg[0xA9] = psize & 0xFF;
        memcpy(&cfg[0xF0], "CMD FD SERIES   ", 16);
        sys_lba = psize;
        total = (uint64_t) psize * 512 + sys.size();
    }
    cfg[0xE2] = 0;                              // no default yet: no partitions
    cfg[0xE3] = 0;

    // An FD is small: zero the data area. A DHD only needs its length - the
    // area before and after the system partition is never read until a
    // partition is created there, and partition:create zeroes it.
    if (!hd)
    {
        std::vector<uint8_t> zero(64 * 512, 0);
        for (uint64_t done = 0; done < (uint64_t) psize * 512; )
        {
            uint32_t n = (uint32_t) std::min<uint64_t>(zero.size(), (uint64_t) psize * 512 - done);
            if (s->write(zero.data(), n) != n)
                return 25;
            done += n;
        }
    }
    if (!s->seek(sys_lba * 512) || s->write(sys.data(), sys.size()) != sys.size())
        return 25;
    if (hd)
    {
        // LBAs 0-127 before the system partition, zeroed
        std::vector<uint8_t> zero(128 * 512, 0);
        if (!s->seek(0) || s->write(zero.data(), zero.size()) != zero.size())
            return 25;
    }
    s->close();
    Debug_printv("new CMD image [%s] %s size[%llu]%s", url.c_str(), hd ? "HD" : "FD", (unsigned long long) total,
                 from_template ? " system partition from " SYSTEM_TEMPLATE : "");
    forget(url);
    return 0;
}

bool DHDImageRegistry::mediaGeometry(const std::string &mediaUrl, uint8_t *type, uint32_t *lbas, uint8_t *err)
{
    std::string lower = mediaUrl;
    for (auto &c : lower) c = tolower(c);
    *err = 64;
    uint32_t base = 0;
    if (mstr::endsWith(lower, ".d64"))      { *type = 2; *lbas = 342;  base = 174848; }
    else if (mstr::endsWith(lower, ".d71")) { *type = 3; *lbas = 683;  base = 349696; }
    else if (mstr::endsWith(lower, ".d81")) { *type = 4; *lbas = 1600; base = 819200; }
    else if (mstr::endsWith(lower, ".dnp")) { *type = 1; }
    else return false;

    std::unique_ptr<MFile> f(MFSOwner::File(mediaUrl, true));
    if (f == nullptr || !f->exists())
    {
        *err = 62;
        return false;
    }
    auto in = f->getSourceStream(std::ios_base::in);
    uint32_t size = in ? in->size() : 0;
    if (*type == 1)
    {
        // a native image is whole 64 KB tracks, 1-255 of them
        if (size == 0 || size % 65536 != 0 || size / 65536 > 255)
            return false;
        *lbas = size / 512;
    }
    else if (size != base && size != base + base / 256)   // plain, or with error info
        return false;
    *err = 0;
    return true;
}

uint8_t DHDImageRegistry::writePartitionImage(const std::string &containerUrl, uint8_t number, const std::string &mediaUrl)
{
    Image *img = obtain(containerUrl);
    if (img == nullptr)
        return 74;
    const DHDPartition *p = number ? img->byNumber(number) : nullptr;
    if (p == nullptr)
        return 77;

    uint8_t type = 0, err = 0;
    uint32_t lbas = 0;
    if (!mediaGeometry(mediaUrl, &type, &lbas, &err))
        return err;
    if (type != p->type)
        return 64;
    if ((uint64_t) lbas * 512 > p->size)
        return 52;

    // Only the image itself: error info past it is dropped.
    uint32_t length = (type == 2) ? 174848 : (type == 3) ? 349696 : (type == 4) ? 819200 : lbas * 512;

    std::unique_ptr<MFile> f(MFSOwner::File(mediaUrl, true));
    auto in = f ? f->getSourceStream(std::ios_base::in) : nullptr;
    if (in == nullptr || !in->isOpen())
        return 62;
    auto out = openRaw(containerUrl, std::ios_base::in | std::ios_base::out);
    if (out == nullptr)
        return 74;

    std::vector<uint8_t> buf(64 * 512);
    for (uint32_t done = 0; done < p->size; )
    {
        uint32_t n = std::min<uint32_t>((uint32_t) buf.size(), p->size - done);
        uint32_t want = done < length ? std::min<uint32_t>(n, length - done) : 0;
        uint32_t got = 0;
        while (got < want)
        {
            uint32_t r = in->read(buf.data() + got, want - got);
            if (r == 0) break;
            got += r;
        }
        if (got < want)
            return 74;
        memset(buf.data() + got, 0, n - got);   // the rest of the window
        if (!out->seek(p->start + done) || out->write(buf.data(), n) != n)
            return 25;
        done += n;
    }
    out->close();
    disposeCachedStream(containerUrl);
    Debug_printv("partition:write [%d] <- [%s] (%lu bytes)", number, mediaUrl.c_str(), (unsigned long) length);
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
