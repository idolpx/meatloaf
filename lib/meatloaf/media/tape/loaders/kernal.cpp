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

#include "kernal.h"
#include <cstring>

// Pulse duration thresholds (in C64 machine cycles * 8)
// Pulse type 0 (short): < 426
// Pulse type 1 (medium): 426-616
// Pulse type 2 (long): > 616
const uint16_t KernalTAPLoader::thresholds[2] = {426, 616};

// Standard C64 tape pilot sequence (decreasing countdown pattern)
const uint8_t KernalTAPLoader::pilot_sequence[9] = {137, 136, 135, 134, 133, 132, 131, 130, 129};

// Convert two pulses to a bit (from kernal_get_bit_func)
// Pulse pattern (0,1) → bit 0
// Pulse pattern (1,0) → bit 1
bool KernalTAPLoader::pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit) const
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

// Read tape header (21 bytes at address 828-848)
// Header structure:
// Byte 0: File type (1 = relocatable, 3 = non-relocatable)
// Bytes 1-2: Start address (little-endian)
// Bytes 3-4: End address (little-endian)
// Bytes 5-20: Filename (16 bytes, PETSCII, space/null-padded)
bool KernalTAPLoader::readTapeHeader(
    uint8_t* header_data,
    uint8_t header_size,
    uint8_t& file_type,
    std::string& filename,
    uint16_t& start_addr,
    uint16_t& end_addr
) const
{
    if (header_size < header_length)
        return false;

    file_type = header_data[0];

    // Validate file type
    if (file_type != 1 && file_type != 3)
        return false;

    // Extract addresses (little-endian)
    start_addr = header_data[1] | (header_data[2] << 8);
    end_addr = header_data[3] | (header_data[4] << 8);

    // Extract filename (16 bytes, null-terminated or space-padded)
    filename.assign((char*)&header_data[5], 16);

    // Trim trailing spaces and nulls
    while (!filename.empty() && (filename.back() == ' ' || filename.back() == '\0'))
        filename.pop_back();

    return true;
}

// Calculate XOR checksum
uint8_t KernalTAPLoader::calculateChecksum(const uint8_t* data, uint16_t size) const
{
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < size; i++)
    {
        checksum ^= data[i];
    }
    return checksum;
}
