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

// .D1M/D2M/D4M - CMD FD2000/FD4000 Disk Image Format
//
// • D1M FD2000/FD4000 DD disk image format  - 800KB  (829440 bytes)
// • D2M FD2000/FD4000 HD disk image format  - 1.6MB  (1658880 bytes)
// • D4M FD4000 ED disk image format         - 3.2MB  (3317760 bytes)
//
// Like the CMD HD (.dhd), FD images hold a partition table in a system
// partition — here at a fixed location (0x640/0xC80/0x1900 512-byte blocks
// for D1M/D2M/D4M), identified by the "CMD FD SERIES" signature at track 0
// sector 5 offset $F0, with the table on track 1 (sys + 2048, up to 31
// partitions). Partition types: 1 = Native (DNP layout), 2 = 1541,
// 3 = 1571, 4 = 1581 — so a D1M resolves to a D81 or DNP decoder based on
// the partition it contains.
//
// Handling is shared with .dhd (see media/hd/dhd.h): the image has a
// currently selected partition (the default partition on first use);
// LOAD"$=P" lists the partitions, CD/LOAD of a partition name or number
// selects it, and the CBM DOS "CP<n>" command changes it.
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC403
// https://web.archive.org/web/20180925144409/https://cbm8bit.com/articles/user-contributions/howto_d1m_d2m_d4m
// https://ist.uwaterloo.ca/~schepers/formats/D2M-DNP.TXT
//


#ifndef MEATLOAF_MEDIA_DXM
#define MEATLOAF_MEDIA_DXM

#include "meatloaf.h"
#include "../hd/dhd.h"


/********************************************************
 * FS
 ********************************************************/

class DXMMFileSystem: public MFileSystem
{
public:
    DXMMFileSystem(): MFileSystem("dxm") {
        vdrive_compatible = true;
    };

    bool handles(std::string fileName) override {
        // Decline while the registry reads the raw image bytes
        if (DHDImageRegistry::probing())
            return false;
        return byExtension({
            ".d1m",
            ".d2m",
            ".d4m"
        }, fileName);
    }

    // Return the MFile type matching the currently selected partition
    // (D81 or DNP for a D1M, any CMD partition type for D2M/D4M)
    MFile* getFile(std::string path) override {
        return DHDCreatePartitionFile(path);
    }
};


#endif /* MEATLOAF_MEDIA_DXM */
