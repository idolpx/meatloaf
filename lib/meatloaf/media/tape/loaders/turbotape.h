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

// Turbo Tape TAP Loader
//
// Turbo Tape 64 - Fast tape loader commonly used in games
// Based on wav-prg turbotape.c implementation
//
// Characteristics:
// - Single threshold: 263 cycles
// - MSB first bit order (different from Kernal's LSB first)
// - Pilot sequence: {9,8,7,6,5,4,3,2,1}
// - Faster than standard Kernal loader
// - XOR checksum

#ifndef MEATLOAF_MEDIA_TAP_LOADER_TURBOTAPE
#define MEATLOAF_MEDIA_TAP_LOADER_TURBOTAPE

#include "loader.h"

class TurboTapeTAPLoader : public TAPLoader {
public:
    TurboTapeTAPLoader() {}
    ~TurboTapeTAPLoader() {}

    std::string getName() const override { return "Turbo Tape 64"; }

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

    // Turbo Tape specific constants
    static const uint16_t thresholds[1];      // {263}
    static const uint8_t pilot_sequence[9];   // {9,8,7,6,5,4,3,2,1}
    static const uint8_t header_length = 20;  // Header size
};

#endif /* MEATLOAF_MEDIA_TAP_LOADER_TURBOTAPE */
