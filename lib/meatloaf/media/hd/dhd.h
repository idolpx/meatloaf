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


/********************************************************
 * Streams
 ********************************************************/

class DHDMStream : public D64MStream {

public:
    struct PartEntry {
        uint8_t number;      // 1-254
        uint8_t type;        // 1=NAT, 2=1541, 3=1571, 4=1581
        std::string name;    // PETSCII, $A0 padding trimmed
        uint32_t start;      // byte offset within image
        uint32_t size;       // bytes
    };

    DHDMStream(std::shared_ptr<MStream> is) : D64MStream(is)
    {
        has_subdirs = true;

        if (readPartitionTable())
        {
            // Configure the default partition so bare paths resolve into it
            selectPartitionByName("");
        }
    };

    virtual uint8_t speedZone(uint8_t track) override
    {
        switch (cur_type)
        {
            case 2: // 1541
                return (track < 18) + (track < 25) + (track < 31);
            case 3: // 1571
                if (track < 35)
                    return (track < 18) + (track < 25) + (track < 31);
                return (track < 53) + (track < 60) + (track < 66);
            default: // native, 1581
                return 0;
        }
    };

    bool hasPartitions() override { return true; }
    bool selectPartitionByName(std::string name) override;
    bool seekPartitionEntry(uint16_t index) override;

protected:
    bool readPartitionTable();
    bool configurePartition(const PartEntry &p);

    std::vector<PartEntry> part_table;
    uint32_t sys_base = 0xFFFFFFFF; // byte offset of system partition
    uint8_t default_part = 1;
    uint8_t cur_type = 1;           // type of currently selected partition

private:
    friend class DHDMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class DHDMFile: public D64MFile {
public:
    DHDMFile(std::string path) : D64MFile(path) {};

    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> is) override
    {
        Debug_printv("[%s]", url.c_str());

        return std::make_shared<DHDMStream>(is);
    }
};



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
        return byExtension(".dhd", fileName);
    }

    MFile* getFile(std::string path) override {
        return new DHDMFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_DHD */
