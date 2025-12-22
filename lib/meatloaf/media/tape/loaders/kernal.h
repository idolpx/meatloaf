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

// Kernal TAP Loader
//
// Standard C64/C16 ROM Kernal tape loader
// Based on wav-prg kernal.c implementation
//
// References:
// - wav-prg: components/wav-prg/loaders/kernal.c
// - Pulse thresholds: C64: {426, 616}, C16: {640, 1260}
// - Pilot sequence: {137,136,135,134,133,132,131,130,129}
// - Header: 21 bytes at address 828
// - Checksum: XOR

#ifndef MEATLOAF_MEDIA_TAP_LOADER_KERNAL
#define MEATLOAF_MEDIA_TAP_LOADER_KERNAL

#include "loader.h"

class KernalTAPLoader : public TAPLoader {
public:
    KernalTAPLoader() {}
    ~KernalTAPLoader() {}

    std::string getName() const override { return "Kernal C64"; }

    const uint16_t* getThresholds() const override { return thresholds; }
    uint8_t getThresholdCount() const override { return 2; }

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

    // Kernal-specific constants
    static const uint16_t thresholds[2];           // {426, 616}
    static const uint8_t pilot_sequence[9];        // {137,136,135,134,133,132,131,130,129}
    static const uint8_t header_length = 21;       // Header size
};

#endif /* MEATLOAF_MEDIA_TAP_LOADER_KERNAL */
