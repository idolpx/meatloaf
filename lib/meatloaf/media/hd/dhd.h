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

// .DHD - CMD Hard drive image format
//
// A DHD image is a raw dump of a CMD HD drive. The system partition is
// located on a 64 KiB boundary and identified by the CMD HD boot magic at
// offset $5F0 of the candidate block (track 0, sector 5, offset $F0). The
// partition table lives on track 1 of the system partition (offset +64 KiB)
// as 32-byte entries: type at +$02, 16-byte $A0-padded name at +$05,
// 3-byte big-endian start LBA (512-byte blocks) at +$15 and size at +$1D.
// Partition types: 1 = Native (DNP layout), 2 = 1541, 3 = 1571, 4 = 1581.
//
// Each image has a "currently selected partition" (the default partition on
// first use, like the real drive). getFile() returns a D64MFile, D71MFile,
// D81MFile or DNPMFile matched to the selected partition's type, decoding a
// window of the image at the partition's offset. The selection changes ONLY
// via the CBM DOS "CP<n>" command or the "partition" console command - the
// real CMD HD does not switch partitions on LOAD or CD, and neither do we.
// LOAD"$=P",8 lists the partitions. A CMD HD holds a MAXIMUM OF 254 partitions,
// numbered 1-254 (CMD FD: 1-31); table entry 0 is the system partition, which
// carries the drive label and the partition table. Entry 0 IS included in the
// listing (type $FF, shown as "sys") because it is a real table entry, but
// select() refuses it - it is not a mountable disk. There is no entry 255:
// vdrive.c's logical slot 255 is this same entry 0 under its own numbering.
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC432
// https://sourceforge.net/p/vice-emu/patches/253/
// https://github.com/c64pectre/c64-cmd-hd
// https://www.pipesup.ca/cmdhd-in-vice/
// https://mikenaberezny.com/hardware/c64-128/cmd-hd-series/
//


#ifndef MEATLOAF_MEDIA_DHD
#define MEATLOAF_MEDIA_DHD

#include "meatloaf.h"
#include "../disk/d64.h"
#include "../disk/d71.h"
#include "../disk/d81.h"
#include "dnp.h"

#include <map>


/********************************************************
 * Partition registry
 ********************************************************/

struct DHDPartition {
    uint8_t number;      // 0-255
    uint8_t type;        // 1=NAT, 2=1541, 3=1571, 4=1581
    std::string name;    // PETSCII, $A0 padding trimmed
    uint32_t start;      // byte offset within image - If 0x00 then look for name.dxx file in the same directory as the image
    uint32_t size;       // bytes
};

// Per-image partition table and selection state, keyed by container URL.
// The table is parsed on first use and the default partition selected.
class DHDImageRegistry {
public:
    struct Image {
        bool valid = false;
        uint8_t default_part = 1;
        uint8_t selected = 0;

        // Which partition of this image the ImageBroker's cached stream
        // currently decodes. The broker keys on the CONTAINER, so it holds one
        // stream per image and cannot tell partitions apart itself - this is
        // how we know a cached stream belongs to a different partition and must
        // be disposed before it is handed to a directory operation. 0 = nothing
        // cached yet.
        uint8_t cached_part = 0;
        std::string disk_label;             // system partition (entry 0) name
        std::vector<DHDPartition> parts;

        const DHDPartition* byNumber(uint8_t number) const;
        const DHDPartition* byName(std::string utf8name) const;
        const DHDPartition* current() const { return byNumber(selected); }
    };

    static Image* obtain(const std::string& containerUrl);
    static bool select(const std::string& containerUrl, uint8_t number);

    // True while the registry reads the raw image (so DHDMFileSystem
    // declines the path and the underlying filesystem serves the bytes)
    static bool probing() { return s_probing; }

    // Path of the ".dhd" container within 'path', or "" if none
    static std::string containerOf(const std::string& path);

    // Drop the ImageBroker's cached stream for this image, so the next access
    // rebuilds it. The key format must mirror ImageBroker::obtain(); keeping it
    // in one place stops the two drifting apart.
    static void disposeCachedStream(const std::string& containerUrl);

private:
    static bool parse(const std::string& containerUrl, Image& img);

    static std::map<std::string, Image> s_images;
    static bool s_probing;
};


// Resolve which partition an in-image path refers to, WITHOUT changing the
// image's selected partition.
//
//   containerUrl  the .dhd/.d1m/.d2m/.d4m path
//   in_path       pathInStream as given
//   out_rest      (optional) the path with any partition component removed
//   out_explicit  (optional) true if in_path actually named a partition
//
// Resolution order for the FIRST component: an in-range number, then byName(),
// otherwise it is not a partition and the currently selected one applies.
// Partition wins over a same-named file; such a file stays reachable as
// "<image>/<partition-number>/<file>".
//
// NOTE the two meanings of 0: a partition NUMBER of 0 in a path means "the
// currently selected partition" (as vdrive.c:1324 does it), NOT table entry 0,
// which is the system partition. Never resolve 0 through byNumber().
//
// Returns nullptr only if the image has no usable partition table.
const DHDPartition* DHDResolvePartition(const std::string& containerUrl,
                                        const std::string& in_path,
                                        std::string* out_rest = nullptr,
                                        bool* out_explicit = nullptr);


/********************************************************
 * Streams
 ********************************************************/

// Fixed-offset window over the raw image: the partition's bytes appear as
// a stand-alone D64/D71/D81/DNP container to the decoding stream.
class DHDOffsetStream : public MStream {
public:
    DHDOffsetStream(std::shared_ptr<MStream> inner, uint32_t offset, uint32_t size)
        : MStream(inner->url), m_inner(inner), m_offset(offset)
    {
        _size = size;
    }

    bool isOpen() override { return m_inner->isOpen(); }
    bool open(std::ios_base::openmode m) override { return m_inner->open(m); }
    void close() override { m_inner->close(); }
    bool isRandomAccess() override { return true; }

    uint32_t read(uint8_t* buf, uint32_t size) override
    {
        if (_position >= _size)
            return 0;
        if (size > _size - _position)
            size = _size - _position;
        uint32_t n = m_inner->read(buf, size);
        _position += n;
        return n;
    }

    uint32_t write(const uint8_t* buf, uint32_t size) override
    {
        if (_position >= _size)
            return 0;
        if (size > _size - _position)
            size = _size - _position;
        uint32_t n = m_inner->write(buf, size);
        _position += n;
        return n;
    }

    bool seek(uint32_t pos) override
    {
        _position = pos;
        return m_inner->seek(m_offset + pos);
    }

private:
    std::shared_ptr<MStream> m_inner;
    uint32_t m_offset;
};


/********************************************************
 * File implementations
 ********************************************************/

// Wraps a disk-type MFile (D64/D71/D81/DNP) so it decodes the currently
// selected partition of the DHD image, and adds partition semantics:
// - "$=P" lists the partitions
// - a leading path component naming a partition does NOT select it; only
//   CP<n> or the "partition" console command changes the selection
template <class BASE>
class DHDPartitionMFile : public BASE {
public:
    DHDPartitionMFile(std::string path) : BASE(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr)
            return nullptr;

        // Every partition here is a FIXED window at a fixed offset inside the
        // container, so none of these streams may ever grow - a DNP that
        // extended itself would write directly over the next partition.
        // D64MStream::allow_grow defaults to false and must stay false on this
        // path; do not set it here.
        auto view = std::make_shared<DHDOffsetStream>(is, p->start, p->size);
        // The decoded stream is scoped to ONE CMD partition; D64MStream's own
        // `partition` field is the sub-partition within that disk and is left
        // alone. Which CMD partition this is belongs to DHDImageRegistry.
        switch (p->type)
        {
            case 2: return std::make_shared<D64MStream>(view);
            case 3: return std::make_shared<D71MStream>(view);
            case 4: return std::make_shared<D81MStream>(view);
            default: return std::make_shared<DNPMStream>(view);
        }
    }

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode = std::ios_base::in) override
    {
        normalizePath();
        return BASE::getSourceStream(mode);
    }

    bool rewindDirectory() override
    {
        normalizePath();
        if (listing_partitions)
        {
            auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(this->url));
            if (img == nullptr)
                return false;
            part_index = 0;
            this->dirIsOpen = true;
            this->media_header = img->disk_label;
            this->media_id = "cmd";
            this->media_blocks_free = 0;
            this->media_image = this->name;
            return true;
        }
        return BASE::rewindDirectory();
    }

    MFile* getNextFileInDir() override
    {
        if (!this->dirIsOpen)
            rewindDirectory();

        if (listing_partitions)
        {
            auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(this->url));
            if (img == nullptr || part_index >= img->parts.size())
            {
                this->dirIsOpen = false;
                return nullptr;
            }

            const DHDPartition &p = img->parts[part_index++];
            std::string fname = p.name;
            mstr::replaceAll(fname, "/", "\\");

            auto file = MFSOwner::File(this->url + "/" + fname);
            file->name = fname;
            static const char *type_label[5] = { "???", "nat", "41", "71", "81" };
            // Type $FF is the system partition (entry 0), which is listed but
            // not selectable; everything else outside 1..4 is an unknown type.
            file->extension = (p.type == 0xFF) ? "sys"
                                               : type_label[(p.type <= 4) ? p.type : 0];
            file->size = p.size;
            file->is_dir = 1;
            return file;
        }
        return BASE::getNextFileInDir();
    }

    bool isDirectory() override
    {
        normalizePath();
        if (listing_partitions)
            return true;
        return BASE::isDirectory();
    }

    bool exists() override
    {
        normalizePath();
        if (listing_partitions)
            return true;
        return BASE::exists();
    }

protected:
    // Handle the partition part of pathInStream once per MFile:
    // "$=P" switches to partition-list mode. A leading path component no
    // longer selects a partition - see the comment inside the method.
    void normalizePath()
    {
        if (normalized)
            return;
        normalized = true;

        if (!this->pathInStream.empty())
        {
            // "$=P" switches to partition-list mode.
            if (mstr::startsWith(this->pathInStream, "$=P") || mstr::startsWith(this->pathInStream, "$=p"))
            {
                listing_partitions = true;
                this->pathInStream.clear();
                return;
            }

            // A leading component naming a partition binds THIS path to that
            // partition and is stripped, so the base class resolves the rest
            // inside it. It deliberately does NOT call select(): naming a
            // partition in a path must not change what the image has selected.
            // That coupling is what once let a directory listing switch
            // partitions part-way through.
            std::string rest;
            bool explicit_part = false;
            const DHDPartition *p = DHDResolvePartition(
                DHDImageRegistry::containerOf(this->url), this->pathInStream, &rest, &explicit_part);

            if (p != nullptr && explicit_part)
            {
                m_part = p->number;
                this->pathInStream = rest;
            }
        }

        // The broker holds ONE stream per image and keys on the container, so
        // it cannot tell partitions apart. If what it has cached decodes a
        // different partition than this path wants, drop it - otherwise the
        // directory operations, which all read the cached stream, would answer
        // for the wrong partition.
        std::string container = DHDImageRegistry::containerOf(this->url);
        auto img = DHDImageRegistry::obtain(container);
        const DHDPartition *want = effectivePartition();
        if (img != nullptr && want != nullptr && img->cached_part != want->number)
        {
            DHDImageRegistry::disposeCachedStream(container);
            img->cached_part = want->number;
        }
    }

    bool normalized = false;
    bool listing_partitions = false;
    uint16_t part_index = 0;

public:
    // The CMD partition this MFile's path names. 0 = none named, so follow the
    // image's current selection - matching vdrive.c:1324, where a partition
    // number of 0 means "the currently selected partition". This is NOT table
    // entry 0, the system partition.
    uint8_t m_part = 0;

    // The partition this MFile actually operates on.
    const DHDPartition* effectivePartition()
    {
        auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(this->url));
        if (img == nullptr)
            return nullptr;
        return (m_part == 0) ? img->current() : img->byNumber(m_part);
    }

    // Name the partition for the broker, so its rebuild resolves the partition
    // THIS path refers to rather than whichever one is selected.
    std::string brokerUrl() override
    {
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr || p->number == 0)
            return this->url;
        return this->url + "/" + std::to_string((unsigned)p->number);
    }

    // Emit entry URLs that name their partition. The base class strips the
    // partition from pathInStream, so a bare entry URL carries none at all and
    // would resolve into the selected partition. The NUMBER is used, not the
    // name: names may contain '/', spaces and PETSCII bytes that do not survive
    // a URL path component.
    std::string entryUrlFor(const std::string& filename) override
    {
        normalizePath();
        const DHDPartition* p = effectivePartition();
        if (p == nullptr || p->number == 0)
            return BASE::entryUrlFor(filename);

        std::string u = this->url;
        u += '/'; u += std::to_string((unsigned)p->number);
        if (this->pathInStream.size()) { u += '/'; u += this->pathInStream; }
        u += '/'; u += filename;
        return u;
    }
};

using DHD41MFile = DHDPartitionMFile<D64MFile>;
using DHD71MFile = DHDPartitionMFile<D71MFile>;
using DHD81MFile = DHDPartitionMFile<D81MFile>;
using DHDNPMFile = DHDPartitionMFile<DNPMFile>;

// Return the MFile type matching the currently selected partition of the
// CMD media image in 'path' (the default partition on first use). Shared
// by the DHD (CMD HD) and D1M/D2M/D4M (CMD FD) filesystems.
//
// This deliberately uses the SELECTED partition, not the one 'path' names, and
// it cannot do otherwise: MFSOwner::File() hands getFile() only the container
// (`meatloaf.cpp`, the getFile(sourcePath) branch) and assigns pathInStream
// AFTERWARDS, so 'path' has no in-image component to parse here.
//
// That is harmless because the base class chosen here is not what decodes the
// data: DHDPartitionMFile overrides getDecodedStream() for all four bases and
// builds the stream from the partition the PATH resolved to. The base class
// only supplies defaults (defaultImageSize, D81MFile's mkDir/rmDir), so naming
// a 1571 partition in a path while a native one is selected yields a
// DHDNPMFile whose stream is nonetheless a correct D71MStream.
// getDecodedStream() is the single authority on partition type.
inline MFile* DHDCreatePartitionFile(std::string path)
{
    uint8_t type = 1;
    auto img = DHDImageRegistry::obtain(DHDImageRegistry::containerOf(path));
    if (img != nullptr)
    {
        const DHDPartition* p = img->current();
        if (p != nullptr)
            type = p->type;
    }

    switch (type)
    {
        case 2: return new DHD41MFile(path);
        case 3: return new DHD71MFile(path);
        case 4: return new DHD81MFile(path);
        default: return new DHDNPMFile(path);
    }
}


/********************************************************
 * FS
 ********************************************************/

class DHDMFileSystem: public MFileSystem
{
public:
    DHDMFileSystem(): MFileSystem("dhd") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        // Decline while the registry reads the raw image bytes
        if (DHDImageRegistry::probing())
            return false;
        return byExtension(".dhd", fileName);
    }

    MFile* getFile(std::string path) override {
        return DHDCreatePartitionFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_DHD */
