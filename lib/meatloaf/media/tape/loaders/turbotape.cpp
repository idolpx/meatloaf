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

#include "turbotape.h"
#include <cstring>

// Single pulse threshold (faster than Kernal)
const uint16_t TurboTapeTAPLoader::thresholds[1] = {263};

// Pilot sequence (countdown from 9 to 1)
const uint8_t TurboTapeTAPLoader::pilot_sequence[9] = {9, 8, 7, 6, 5, 4, 3, 2, 1};

// Turbo Tape uses same bit encoding as Kernal
// Pulse pattern (0,1) → bit 0
// Pulse pattern (1,0) → bit 1
bool TurboTapeTAPLoader::pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit) const
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

// Read Turbo Tape header
// Header structure (20 bytes):
// Byte 0: File type (1, 2, or 0x61)
// Bytes 1-2: Start address (little-endian)
// Bytes 3-4: End address (little-endian)
// Byte 5: Unknown
// Bytes 6-21: Filename (16 bytes, PETSCII)
bool TurboTapeTAPLoader::readTapeHeader(
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
    if (file_type != 1 && file_type != 2 && file_type != 0x61)
        return false;

    // Extract addresses (little-endian)
    start_addr = header_data[1] | (header_data[2] << 8);
    end_addr = header_data[3] | (header_data[4] << 8);

    // Skip byte 5 (unknown purpose)

    // Extract filename (16 bytes starting at offset 6)
    filename.assign((char*)&header_data[6], 16);

    // Trim trailing spaces and nulls
    while (!filename.empty() && (filename.back() == ' ' || filename.back() == '\0'))
        filename.pop_back();

    return true;
}

// Calculate XOR checksum (same as Kernal)
uint8_t TurboTapeTAPLoader::calculateChecksum(const uint8_t* data, uint16_t size) const
{
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < size; i++)
    {
        checksum ^= data[i];
    }
    return checksum;
}
