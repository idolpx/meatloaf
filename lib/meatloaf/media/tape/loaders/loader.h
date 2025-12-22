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

// TAP Loader Interface
//
// This defines the base interface for TAP loaders. Different loaders
// (Kernal, Turbo Tape, Novaload, etc.) can be implemented as subclasses.

#ifndef MEATLOAF_MEDIA_TAP_LOADER
#define MEATLOAF_MEDIA_TAP_LOADER

#include <string>
#include <cstdint>

// Base class for TAP loaders
class TAPLoader {
public:
    virtual ~TAPLoader() {}

    // Get loader name
    virtual std::string getName() const = 0;

    // Get pulse thresholds (array of threshold values)
    virtual const uint16_t* getThresholds() const = 0;
    virtual uint8_t getThresholdCount() const = 0;

    // Convert two pulses to a bit
    virtual bool pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit) const = 0;

    // Read tape header
    virtual bool readTapeHeader(
        uint8_t* header_data,  // Buffer containing header bytes
        uint8_t header_size,   // Size of header
        uint8_t& file_type,    // Output: file type
        std::string& filename, // Output: filename
        uint16_t& start_addr,  // Output: start address
        uint16_t& end_addr     // Output: end address
    ) const = 0;

    // Calculate checksum
    virtual uint8_t calculateChecksum(const uint8_t* data, uint16_t size) const = 0;
};

#endif /* MEATLOAF_MEDIA_TAP_LOADER */
