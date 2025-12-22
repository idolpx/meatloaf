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

#include "ocean.h"
#include <cstring>

// Single pulse threshold
const uint16_t OceanTAPLoader::thresholds[1] = {480};

// Ocean uses same bit encoding as Kernal
bool OceanTAPLoader::pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit) const
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

// Read Ocean header
// Header structure (minimal):
// Byte 0: Initial byte (unknown)
// Byte 1: Start block number (page-aligned, multiply by 256)
// Data is loaded in 256-byte blocks
// Ocean tapes typically don't have filenames in the header
bool OceanTAPLoader::readTapeHeader(
    uint8_t* header_data,
    uint8_t header_size,
    uint8_t& file_type,
    std::string& filename,
    uint16_t& start_addr,
    uint16_t& end_addr
) const
{
    if (header_size < 2)
        return false;

    // Byte 0 is unknown/unused
    // Byte 1 contains the start block number (page-aligned)
    uint8_t start_block = header_data[1];

    // Calculate start address (block * 256)
    start_addr = start_block * 256;

    // Ocean doesn't specify end address in header
    // It loads blocks sequentially until EOF marker
    // For now, use a placeholder
    end_addr = start_addr + block_size;

    // Ocean tapes typically don't have filenames
    filename = "OCEAN";

    // File type is always PRG
    file_type = 1;

    return true;
}

// Calculate XOR checksum
uint8_t OceanTAPLoader::calculateChecksum(const uint8_t* data, uint16_t size) const
{
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < size; i++)
    {
        checksum ^= data[i];
    }
    return checksum;
}
