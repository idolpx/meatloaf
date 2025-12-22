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

// Novaload TAP Loader
//
// Novaload - Copy protection and fast loader system
// Based on wav-prg novaload.c implementation
//
// Characteristics:
// - Single threshold: 500 cycles
// - Pilot byte: 0xAA
// - Variable length filename (1-16 chars)
// - 256-byte data blocks with checksums between blocks
// - XOR checksum
// - Used by many commercial games for copy protection

#ifndef MEATLOAF_MEDIA_TAP_LOADER_NOVALOAD
#define MEATLOAF_MEDIA_TAP_LOADER_NOVALOAD

#include "loader.h"

class NovaloadTAPLoader : public TAPLoader {
public:
    NovaloadTAPLoader() {}
    ~NovaloadTAPLoader() {}

    std::string getName() const override { return "Novaload"; }

    const uint16_t* getThresholds() const override { return thresholds; }
    uint8_t getThresholdCount() const override { return 1; }  // Single threshold

    bool pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit) const override;

    bool readTapeHeader(
        uint8_t* header_data,
        uint8_t header_size,
        uint8_t& file_type,
        std::string& filename,
        uint16_t& start_addr,
        uint16_t& end_addr
    ) const override;

    uint8_t calculateChecksum(const uint8_t* data, uint16_t size) const override;

    // Novaload specific constants
    static const uint16_t thresholds[1];     // {500}
    static const uint8_t pilot_byte = 0xAA; // Pilot pattern
    static const uint8_t max_filename_length = 16;
};

#endif /* MEATLOAF_MEDIA_TAP_LOADER_NOVALOAD */
