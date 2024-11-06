#include "g64.h"

#include "utils.h"

/* GCR-to-Nibble conversion tables */
static uint8_t GCR_decode_high[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x80, 0x00, 0x10, 0xff, 0xc0, 0x40, 0x50,
	0xff, 0xff, 0x20, 0x30, 0xff, 0xf0, 0x60, 0x70,
	0xff, 0x90, 0xa0, 0xb0, 0xff, 0xd0, 0xe0, 0xff
};

static uint8_t GCR_decode_low[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x08, 0x00, 0x01, 0xff, 0x0c, 0x04, 0x05,
	0xff, 0xff, 0x02, 0x03, 0xff, 0x0f, 0x06, 0x07,
	0xff, 0x09, 0x0a, 0x0b, 0xff, 0x0d, 0x0e, 0xff
};

// G64 Utility Functions

bool G64MStream::seekBlock(uint64_t index, uint8_t offset)
{
    uint16_t sectorOffset = 0;
    uint8_t track = 0;

    // Debug_printv("track[%d] sector[%d] offset[%d]", track, sector, offset);

    // Determine actual track & sector from index
    do
    {
        track++;
        uint8_t count = getSectorCount(track);
        if (sectorOffset + count < index)
            sectorOffset += count;
        else
            break;

        // Debug_printv("track[%d] speedZone[%d] secotorsPerTrack[%d] sectorOffset[%d]", track, speedZone(track), count, sectorOffset);
    } while (true);
    uint8_t sector = index - sectorOffset;

    this->block = index;
    this->track = track;
    this->sector = sector;

    // Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek((index * block_size) + offset);
}

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

    uint8_t g64_track = (track * 2);
    uint32_t g64_track_offset = TRACK_TABLE_OFFSET + (g64_track * 4);
    containerStream->seek(g64_track_offset);
    readContainer((uint8_t *)&g64_track_offset, sizeof(g64_track_offset));

    uint16_t g64_track_size = 0x00;
    containerStream->seek(g64_track_offset);
    readContainer((uint8_t *)&g64_track_size, sizeof(g64_track_size));
    g64_track_offset += 2;
    uint32_t g64_track_end = g64_track_offset + g64_track_size;


    track++;

    sector = 0;
    while ( sector < c)
    {
        sectorOffset = g64_track_offset + (sector * 361);
        containerStream->seek( sectorOffset );

        Debug_printv("track[%d] g64_track[%d] g64_track_offset[%04X] g64_track_size[%d]", track, (g64_track + 2), g64_track_offset, g64_track_size);
        Debug_printv("g64_track_end[%04X] sector_offset[%04X]", g64_track_end, sectorOffset);

        findSync( g64_track_end );
        Debug_printv( "Read Sector Header" );
        readSectorHeader();
        findSync( g64_track_end );
        Debug_printv( "Read Sector Data" );

        sector++;
    }

    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;

    //Debug_printv("track[%d] sector[%d] speedZone[%d] sectorOffset[%d]", track, sector, speedZone(track), sectorOffset);

    return containerStream->seek( sectorOffset );
}


 uint32_t G64MStream::readContainer(uint8_t *buf, uint32_t size)
{
    return containerStream->read(buf, size);
}

bool G64MStream::readSectorHeader()
{
    uint8_t buf[5] = { 0x00 };
    uint8_t data[4] = { 0x00 };

    containerStream->read(buf, sizeof(buf));
    convert4BytesFromGCR(buf, data);
    printf("H[%02X] T[%02X] S[%02X] ID0[%02X] ", data[0], data[1], data[2], data[3]);

    containerStream->read(buf, sizeof(buf));
    convert4BytesFromGCR(buf, data);
    printf("ID1[%02X] C[%02X] G0[%02X] G1[%02X]\r\n", data[0], data[1], data[2], data[3]);

    return true;
}

bool G64MStream::readSector()
{
    uint8_t buf[5] = { 0x00 };
    uint8_t data[4] = { 0x00 };

    containerStream->read(buf, sizeof(buf));
    util_dump_bytes(buf, sizeof(buf));
    convert4BytesFromGCR(buf, data);
    util_dump_bytes(data, sizeof(data));

    return true;
}


bool G64MStream::findSync(uint32_t track_end)
{
    uint8_t gcr_byte0 = 0x00;
    uint8_t gcr_byte1 = 0x00;

	while (1)
	{
		if (containerStream->position() + 1 >= track_end)
		{
			containerStream->position(track_end);
            Debug_printv("sync not found");
			return false;	/* not found */
		}

        containerStream->read((uint8_t *)&gcr_byte1, sizeof(gcr_byte1));
        Debug_printf("%02X ", gcr_byte1);

		// sync flag goes up after the 10th bit
		if ((gcr_byte0 & 0x03) == 0x03 && (gcr_byte1 == 0xff))
			break;
        
        gcr_byte0 = gcr_byte1;
	}

    Debug_print("[sync!] ");

    do
    {
        Debug_printf("%02X ", gcr_byte1);
	    containerStream->read((uint8_t *)&gcr_byte1, sizeof(gcr_byte1));
    } while (containerStream->position() < track_end && gcr_byte1 == 0xff);
    containerStream->seek(containerStream->position() - 1);

    Debug_printf("\r\n");

	return true;
}


int G64MStream::convert4BytesFromGCR(uint8_t * gcr, uint8_t * plain)
{
	uint8_t hnibble, lnibble;
	int badGCR, nConverted;

	badGCR = 0;

	hnibble = GCR_decode_high[gcr[0] >> 3];
	lnibble = GCR_decode_low[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 1;
	*plain++ = hnibble | lnibble;

	hnibble = GCR_decode_high[(gcr[1] >> 1) & 0x1f];
	lnibble = GCR_decode_low[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 2;
	*plain++ = hnibble | lnibble;

	hnibble = GCR_decode_high[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f];
	lnibble = GCR_decode_low[(gcr[3] >> 2) & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 3;
	*plain++ = hnibble | lnibble;

	hnibble = GCR_decode_high[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f];
	lnibble = GCR_decode_low[gcr[4] & 0x1f];
	if ((hnibble == 0xff || lnibble == 0xff) && !badGCR)
		badGCR = 4;
	*plain++ = hnibble | lnibble;

	nConverted = (badGCR == 0) ? 4 : (badGCR - 1);

	return (nConverted);
}