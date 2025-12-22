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

#include "novaload.h"
#include <cstring>

// Single pulse threshold
const uint16_t NovaloadTAPLoader::thresholds[1] = {500};

// Novaload uses same bit encoding as Kernal
bool NovaloadTAPLoader::pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit) const
{
    if (pulse1 == 0 && pulse2 == 1)
    {
        bit = 0;
        return true;
    }
    if (pulse1 == 1 && pulse2 == 0)
    {
        bit = 1;
        return true;
    }
    return false;
}

// Read Novaload header
// Header structure (variable length):
// Byte 0: Filename length (1-16)
// Bytes 1-N: Filename (variable, PETSCII)
// Bytes N+1-N+2: Start address (little-endian)
// Bytes N+3-N+4: Unused (end address, not used by C64 implementation)
// Bytes N+5-N+6: Block length (little-endian)
bool NovaloadTAPLoader::readTapeHeader(
    uint8_t* header_data,
    uint8_t header_size,
    uint8_t& file_type,
    std::string& filename,
    uint16_t& start_addr,
    uint16_t& end_addr
) const
{
    if (header_size < 7)  // Minimum: 1 (length) + 1 (name) + 2 (start) + 2 (unused) + 2 (len)
        return false;

    // Read filename length
    uint8_t namelen = header_data[0];

    // Validate filename length (prevent false detections)
    if (namelen == 0 || namelen > max_filename_length)
        return false;

    if (header_size < namelen + 7)
        return false;

    // Extract filename
    filename.assign((char*)&header_data[1], namelen);

    // Extract start address
    uint8_t offset = 1 + namelen;
    start_addr = header_data[offset] | (header_data[offset + 1] << 8);

    // Skip unused end address bytes (offset + 2, offset + 3)

    // Extract block length
    uint16_t blocklen = header_data[offset + 4] | (header_data[offset + 5] << 8);

    // Validate block length
    if (blocklen < 256 || start_addr + blocklen > 65536)
        return false;

    // Calculate actual end address
    // Novaload has 256-byte header block that's skipped
    end_addr = start_addr + blocklen;
    start_addr += 256;

    // File type is always PRG for Novaload
    file_type = 1;

    return true;
}

// Calculate XOR checksum
uint8_t NovaloadTAPLoader::calculateChecksum(const uint8_t* data, uint16_t size) const
{
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < size; i++)
    {
        checksum ^= data[i];
    }
    return checksum;
}
