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

#include "g64.h"

#include <cstring>

#include "utils.h"

/* G64-to-Nibble conversion tables */
static uint8_t gcr_decode_high[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x80, 0x00, 0x10, 0xff, 0xc0, 0x40, 0x50,
	0xff, 0xff, 0x20, 0x30, 0xff, 0xf0, 0x60, 0x70,
	0xff, 0x90, 0xa0, 0xb0, 0xff, 0xd0, 0xe0, 0xff
};

static uint8_t gcr_decode_low[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x08, 0x00, 0x01, 0xff, 0x0c, 0x04, 0x05,
	0xff, 0xff, 0x02, 0x03, 0xff, 0x0f, 0x06, 0x07,
	0xff, 0x09, 0x0a, 0x0b, 0xff, 0x0d, 0x0e, 0xff
};

// GCR Utility Functions

bool G64MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    uint32_t sectorOffset = 0;

    Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Is this a valid track?
    uint16_t c = partitions[partition].block_allocation_map.size() - 1;
    uint8_t start_track = partitions[partition].block_allocation_map[0].start_track;
    uint8_t end_track = partitions[partition].block_allocation_map[c].end_track;
    if (track < start_track || track > end_track)
    {
        Debug_printv("Invalid Track: track[%d] start_track[%d] end_track[%d]", track, start_track, end_track);
        return false;
    }

    // Is this a valid sector?
    c = getSectorCount(track);
    if (sector > c)
    {
        Debug_printv("Invalid Sector: sector[%d] sectorsPerTrack[%d]", sector, c);
        return false;
    }


    track--;

    uint8_t gcr_track = (track * 2);
    uint32_t gcr_track_offset = TRACK_TABLE_OFFSET + (gcr_track * 4);
    containerStream->seek(gcr_track_offset);
    containerStream->read((uint8_t *)&gcr_track_offset, sizeof(gcr_track_offset));

    uint16_t gcr_track_size = 0x00;
    containerStream->seek(gcr_track_offset);
    containerStream->read((uint8_t *)&gcr_track_size, sizeof(gcr_track_size));
    gcr_track_offset += 2;
    uint32_t gcr_track_end = gcr_track_offset + gcr_track_size;

    track++;

    sectorOffset = gcr_track_offset + (sector * 360);
    containerStream->seek( sectorOffset );
    do
    {
        //Debug_printv("track[%d] gcr_track[%d] gcr_track_offset[%04X] gcr_track_size[%d]", track, (gcr_track + 2), gcr_track_offset, gcr_track_size);
        //Debug_printv("gcr_track_end[%04X] sector_offset[%04X]", gcr_track_end, sectorOffset);

        findSync( gcr_track_end );
        //Debug_printv( "Read Sector Header [%04X]", containerStream->position() );
        readSectorHeader();
        findSync( gcr_track_end );
        //Debug_printv( "Read Sector Data [%04X]", containerStream->position() );
    } 
    while ( sector != gcr_sector_header.sector );
    Debug_printv( "Start Sector Data [%04lX]", containerStream->position() );
    readSector();

    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;
    _position = offset;

    //Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return true;
}


 uint32_t G64MStream::readContainer(uint8_t *buf, uint32_t size)
{
    uint8_t *s = sector_buffer + _position;
    std::memcpy(buf, s, size);
    _position += size;
    return size;
}

bool G64MStream::readSectorHeader()
{
    uint8_t buf[5] = { 0x00 };
    uint8_t data[4] = { 0x00 };

    containerStream->read(buf, sizeof(buf));
    convert4BytesFromGCR(buf, data);
    gcr_sector_header.code = data[0];
    gcr_sector_header.checksum = data[1];
    gcr_sector_header.sector = data[2];
    gcr_sector_header.track = data[3];
    printf("H[%02X] C[%02X] S[%d] T[%d] ", data[0], data[1], data[2], data[3]);

    containerStream->read(buf, sizeof(buf));
    convert4BytesFromGCR(buf, data);
    gcr_sector_header.id1 = data[0];
    gcr_sector_header.id0 = data[1];
    printf("ID1[%02X] ID0[%02X]", data[0], data[1]);

    uint8_t checksum = gcr_sector_header.sector ^ gcr_sector_header.track ^ gcr_sector_header.id1 ^ gcr_sector_header.id0;

    printf(" VERIFY[%02X]\r\n", checksum);
    return true;
}

bool G64MStream::readSector()
{
    uint8_t buf[5] = { 0x00 };
    uint8_t data[4] = { 0x00 };
    uint8_t *d = sector_buffer;

    containerStream->read(buf, sizeof(buf));
    convert4BytesFromGCR(buf, data);

    // Data Header 0x07
    if ( data[0] == 0x07 )
    {
        uint8_t *h = data + 1;
        std::memcpy(d, h, 3);  // skip the header byte
        d += 3;

        for ( int i = 0; i< 64; i++ )
        {
            containerStream->read(buf, sizeof(buf));
            convert4BytesFromGCR(buf, data);
            std::memcpy(d, &data, 4);
            d += 4;
        }
        util_dump_bytes(sector_buffer, sizeof(sector_buffer));
    }

    return true;
}


bool G64MStream::findSync(uint32_t track_end)
{
    uint8_t gcr_byte0 = 0x00;
    uint8_t gcr_byte1 = 0x00;

    //Debug_printv( "start[%04X]", containerStream->position() );
	while (1)
	{
		if (containerStream->position() + 1 >= track_end)
		{
			containerStream->position(track_end);
            //Debug_printv("sync not found");
			return false;	/* not found */
		}

        containerStream->read((uint8_t *)&gcr_byte1, sizeof(gcr_byte1));
        //Debug_printf("%02X ", gcr_byte1);

		// sync flag goes up after the 10th bit
		if ((gcr_byte0 & 0x03) == 0x03 && (gcr_byte1 == 0xff))
			break;
        
        gcr_byte0 = gcr_byte1;
	}

    //Debug_print("[sync!] ");

    do
    {
        //Debug_printf("%02X ", gcr_byte1);
	    containerStream->read((uint8_t *)&gcr_byte1, sizeof(gcr_byte1));
    } while (containerStream->position() < track_end && gcr_byte1 == 0xff);
    containerStream->seek(containerStream->position() - 1);

    //Debug_printf("\r\n");

    //Debug_printv( "end[%04X]", containerStream->position() );

	return true;
}


int G64MStream::convert4BytesFromGCR(uint8_t * gcr, uint8_t * plain)
{
	uint8_t hnibble, lnibble;
	int badGCR, nConverted;

	badGCR = 0;

	hnibble = gcr_decode_high[gcr[0] >> 3];
	lnibble = gcr_decode_low[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 1;
	*plain++ = hnibble | lnibble;

	hnibble = gcr_decode_high[(gcr[1] >> 1) & 0x1f];
	lnibble = gcr_decode_low[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 2;
	*plain++ = hnibble | lnibble;

	hnibble = gcr_decode_high[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f];
	lnibble = gcr_decode_low[(gcr[3] >> 2) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 3;
	*plain++ = hnibble | lnibble;

	hnibble = gcr_decode_high[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f];
	lnibble = gcr_decode_low[gcr[4] & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 4;
	*plain++ = hnibble | lnibble;

	nConverted = (badGCR == 0) ? 4 : (badGCR - 1);

	return (nConverted);
}