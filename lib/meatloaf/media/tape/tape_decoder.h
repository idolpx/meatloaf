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

// Tape image analysis driver built on the wav2prg core (WAV-PRG 4.2.1 by
// Fabrizio Gennari, GPL). Decodes .tap/.dmp/.htap pulse streams into PRG
// files, detecting the loader (Kernal, Turbo Tape 64, Freeload, Novaload,
// Pavloda, Ocean, ... - all wav-prg plugins are statically linked).
//
// Formats:
//  .tap  - "C64-TAPE-RAW", v0/v1 pulses (v2 = halfwaves)
//  .dmp  - "DC2N-TAP-RAW", 16-bit samples at counter_rate (usually 2 MHz)
//  .htap - "-HIRES" at offset 6, signed 16-bit halfwaves at 0.5 us; pauses
//          as 0x0000 0x0000 + 32-bit us (Manosoft HTAP spec V0 sub 2.0)

#ifndef MEATLOAF_MEDIA_TAPE_DECODER
#define MEATLOAF_MEDIA_TAPE_DECODER

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class MStream;

struct TapeEntry {
    std::string name;       // block name (PETSCII, trimmed)
    std::string loader;     // detected loader name (wav2prg plugin name)
    uint16_t start_addr = 0;
    uint16_t end_addr = 0;
    uint32_t tape_offset = 0;     // byte offset of the block's sync in the image
    uint32_t tape_end_offset = 0; // byte offset just past the block
    uint32_t start_time_ms = 0;   // tape counter time at block start
    uint32_t end_time_ms = 0;     // tape counter time at block end
    bool checksum_ok = false;
    std::vector<uint8_t> prg;   // 2-byte load address + data
};

class TapeDecoder {
public:
    // Analyze a tape image held in memory. start_offset skips to a byte
    // offset within the image (used with .idx files); single_program stops
    // after the first complete program chain.
    static bool analyze(const uint8_t *image, uint32_t image_len,
                        uint32_t start_offset, bool single_program,
                        std::vector<TapeEntry> &entries);

    // True if the buffer looks like a supported tape image
    static bool isTapeImage(const uint8_t *image, uint32_t image_len);

    // Fill in start/end times of entries (tape counter) and return the
    // total tape duration in ms. Walks the pulse stream once.
    static uint32_t computeTimes(const uint8_t *image, uint32_t image_len,
                                 std::vector<TapeEntry> &entries);
};

#endif /* MEATLOAF_MEDIA_TAPE_DECODER */
