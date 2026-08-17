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

#ifndef MEATLOAF_MEDIA_MFM
#define MEATLOAF_MEDIA_MFM

#include <cstdint>

// Reading IBM System 34 MFM sectors out of a cell bitstream.
//
// Two formats in this tree end up holding exactly the same thing - a byte
// packed stream of MFM cells, MSB first - by completely different routes: a
// .p81 decodes it from flux pulses (see p81.h), a .g81 reads it straight out of
// the container (see g81.h). Everything from that point on is identical, and
// this is that everything.
//
// What is deliberately NOT here is the logical-to-physical sector mapping. That
// `head = block / 20, sector = ((block % 20) / 2) + 1` arithmetic is 1581
// geometry, and each format's relationship between its own track/side numbering
// and the physical cylinder/head is its own affair - the .p81 side bit is
// inverted, for instance, which is a P64 container quirk and not a property of
// the drive. Keeping the mapping at each call site keeps those assumptions
// visible where they are made instead of looking validated in a shared helper.

namespace mfm
{
    // The raw cell pattern of an $A1 sync byte with its missing clock bit. It
    // cannot occur in encoded data, which is what makes it a sync.
    static const uint16_t SYNC_PATTERN = 0x4489;

    // IBM System 34 address marks.
    static const uint8_t MARK_ID = 0xFE;
    static const uint8_t MARK_DATA = 0xFB;
    static const uint8_t MARK_DELETED_DATA = 0xF8;

    // Finds the next sync at or after cell p, giving up after `limit` cells.
    // Returns the cell index just past the sync, or -1. Wraps at the end of the
    // stream, since a track is a loop.
    int findSync( const uint8_t *bits, uint32_t bytes, int p, int limit );

    // Reads `count` MFM data bytes starting at cell p. Cells alternate clock,
    // data, clock, data - the data bit is the second of each pair.
    bool readBytes( const uint8_t *bits, uint32_t bytes, int p, uint8_t *buf, int count );

    // CRC-16/CCITT, the one a WD177x computes over the address mark bytes and
    // the payload that follows them.
    uint16_t crc16( const uint8_t *data, uint32_t length, uint16_t crc = 0xffff );

    // Finds the sector with this cylinder/head/sector id and copies its data
    // out. `size` is the sector's payload length - 512 on a 1581.
    //
    // A header whose CRC does not verify is skipped rather than trusted, which
    // is what rejects a false sync in a gap. A DATA crc failure is reported
    // through `crc_ok` and the bytes are handed over anyway: an original disk
    // can carry deliberately bad CRCs and the caller has no error channel.
    bool readSector( const uint8_t *bits, uint32_t bytes,
                     uint8_t cylinder, uint8_t head, uint8_t sector,
                     uint8_t *out, uint32_t size, bool *crc_ok );
}

#endif /* MEATLOAF_MEDIA_MFM */
