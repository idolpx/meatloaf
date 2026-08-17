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
//
// The range decoder and the pulse-to-GCR logic below are ports of Benjamin
// 'BeRo' Rosseaux's P64 reference implementation (zlib licence), as used by
// VICE. See .reference/p64conv/lib/p64refimp/ for the original.

#include "p64.h"

#include <cstdlib>
#include <cstring>

/********************************************************
 * GCR nibble decoding
 ********************************************************/

// Same tables g64.cpp carries. They are duplicated rather than shared because
// the two formats decode from different substrates - g64 reads GCR bytes out of
// the container, this reads them out of a bitstream we synthesized in RAM - so
// there is no common code path to hang them off, only a common 64 bytes.
static const uint8_t gcr_decode_high[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x80, 0x00, 0x10, 0xff, 0xc0, 0x40, 0x50,
    0xff, 0xff, 0x20, 0x30, 0xff, 0xf0, 0x60, 0x70,
    0xff, 0x90, 0xa0, 0xb0, 0xff, 0xd0, 0xe0, 0xff
};

static const uint8_t gcr_decode_low[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x08, 0x00, 0x01, 0xff, 0x0c, 0x04, 0x05,
    0xff, 0xff, 0x02, 0x03, 0xff, 0x0f, 0x06, 0x07,
    0xff, 0x09, 0x0a, 0x0b, 0xff, 0x0d, 0x0e, 0xff
};

// 5 GCR bytes -> 4 plain bytes. Invalid GCR nibbles decode to 0xf0/0x0f rather
// than failing: a false sync inside a gap yields garbage that the header
// checksum then rejects, which is the check that matters.
static void gcr_to_bytes(const uint8_t *gcr, uint8_t *plain)
{
    plain[0] = gcr_decode_high[gcr[0] >> 3]
             | gcr_decode_low[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f];
    plain[1] = gcr_decode_high[(gcr[1] >> 1) & 0x1f]
             | gcr_decode_low[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f];
    plain[2] = gcr_decode_high[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f]
             | gcr_decode_low[(gcr[3] >> 2) & 0x1f];
    plain[3] = gcr_decode_high[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f]
             | gcr_decode_low[gcr[4] & 0x1f];
}


/********************************************************
 * Container helpers
 ********************************************************/

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// MStream::read() returns at most one block, so a caller wanting `len` bytes
// has to loop until it has them or the stream stops giving.
static bool read_at(MStream *stream, uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (stream == nullptr || !stream->seek(offset))
        return false;

    uint32_t got = 0;
    while (got < len)
    {
        uint32_t n = stream->read(buf + got, len - got);
        if (n == 0)
            return false;
        got += n;
    }
    return true;
}


/********************************************************
 * Range decoder
 *
 * FPAQ0-style carryless range coder with adaptive 12-bit models, one model per
 * byte of each value, one bit at a time. Ported from P64PulseStreamReadFromStream.
 ********************************************************/

#define MODEL_POSITION       0
#define MODEL_STRENGTH       4
#define MODEL_POSITION_FLAG  8
#define MODEL_STRENGTH_FLAG  9
#define MODEL_COUNT          10

// 65536 contexts for each byte of the two 32-bit values, 4 for each 1-bit flag.
static const uint32_t model_sizes[MODEL_COUNT] = {
    65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536, 4, 4
};

// 524296 entries. Held as uint16_t rather than the reference's uint32_t - a
// probability lives in 15..4080 with a shift of 4 and an initial 2048, so 12
// bits is the whole range - which halves the table to 1 MB.
static const uint32_t model_total = (65536 * 8) + 4 + 4;

namespace {

struct RangeDecoder
{
    const uint8_t *buffer = nullptr;
    uint32_t buffer_size = 0;
    uint32_t buffer_pos = 0;

    uint32_t code = 0;
    uint32_t low = 0;
    uint32_t high = 0xffffffffUL;

    uint16_t *probabilities = nullptr;
    uint32_t offsets[MODEL_COUNT] = { 0 };
    uint32_t states[MODEL_COUNT] = { 0 };

    void start(const uint8_t *data, uint32_t size, uint16_t *probs)
    {
        buffer = data;
        buffer_size = size;
        buffer_pos = 0;
        code = 0;
        low = 0;
        high = 0xffffffffUL;
        probabilities = probs;

        uint32_t total = 0;
        for (uint32_t i = 0; i < MODEL_COUNT; i++)
        {
            offsets[i] = total;
            total += model_sizes[i];
            states[i] = 0;
        }

        for (uint32_t i = 0; i < 4; i++)
            code = (code << 8) | nextByte();
    }

    uint8_t nextByte()
    {
        return (buffer_pos < buffer_size) ? buffer[buffer_pos++] : 0;
    }

    uint32_t decodeBit(uint16_t *probability)
    {
        uint32_t middle = low + (((high - low) >> 12) * (uint32_t)*probability);
        uint32_t bit;

        if (code <= middle)
        {
            *probability = (uint16_t)(*probability + ((0xfffUL - *probability) >> 4));
            high = middle;
            bit = 1;
        }
        else
        {
            *probability = (uint16_t)(*probability - (*probability >> 4));
            low = middle + 1;
            bit = 0;
        }

        while (!((low ^ high) & 0xff000000UL))
        {
            low <<= 8;
            high = (high << 8) | 0xffUL;
            code = (code << 8) | nextByte();
        }

        return bit;
    }

    // A 1-bit flag: the model's own state IS the context.
    uint32_t readFlag(uint32_t model)
    {
        states[model] = decodeBit(probabilities + offsets[model] + states[model]);
        return states[model];
    }

    // A 32-bit value, little endian, byte by byte. The context of each bit is
    // the previous byte's value shifted up under the bits decoded so far.
    uint32_t readDWord(uint32_t model)
    {
        uint32_t value = 0;
        for (uint32_t byte_index = 0; byte_index < 4; byte_index++)
        {
            uint32_t context = 1;
            for (int bit = 7; bit >= 0; bit--)
            {
                uint32_t slot = ((states[model + byte_index] << 8) | context) & 0xffffUL;
                context = (context << 1) | decodeBit(probabilities + offsets[model + byte_index] + slot);
            }
            uint32_t byte_value = context & 0xffUL;
            states[model + byte_index] = byte_value;
            value |= byte_value << (byte_index << 3);
        }
        return value;
    }
};

} // namespace


/********************************************************
 * P64MStream
 ********************************************************/

bool P64MStream::readHeader()
{
    if (!parseChunks())
        return false;

    return D64MStream::readHeader();
}

bool P64MStream::parseChunks()
{
    // Idempotent, and deliberately latches on failure too: readHeader() runs on
    // every directory rewind, and re-walking a chunk stream that is not going
    // to parse costs a request per chunk every time.
    if (chunks_parsed)
        return chunks_ok;

    chunks_parsed = true;
    chunks_ok = false;
    half_tracks.clear();

    MStream *cs = containerStream.get();

    uint8_t header[P64_HEADER_SIZE];
    if (!read_at(cs, 0, header, sizeof(header)))
    {
        Debug_printv("cannot read P64 header");
        return false;
    }

    if (std::memcmp(header, imageSignature(), 8) != 0)
    {
        Debug_printv("not a %s image", imageSignature());
        return false;
    }

    uint32_t version = le32(header + 8);
    if (version != 0)
    {
        Debug_printv("unsupported P64 version [%lu]", (unsigned long)version);
        return false;
    }

    // Flags bit 0 is the image's write-protect bit; it is not consulted,
    // because writeContainer() refuses every write regardless.
    uint32_t stream_size = le32(header + 16);

    uint32_t container_size = cs->size();
    uint32_t end = P64_HEADER_SIZE + stream_size;
    if (container_size > 0 && end > container_size)
        end = container_size;

    uint32_t offset = P64_HEADER_SIZE;

    while (offset + P64_CHUNK_HEADER_SIZE <= end)
    {
        uint8_t chunk[P64_CHUNK_HEADER_SIZE];
        if (!read_at(cs, offset, chunk, sizeof(chunk)))
            break;

        uint32_t chunk_size = le32(chunk + 4);
        uint32_t data_start = offset + P64_CHUNK_HEADER_SIZE;

        if (std::memcmp(chunk, "DONE", 4) == 0 && chunk_size == 0)
            break;

        if (data_start + chunk_size > end)
        {
            Debug_printv("chunk at [%lu] runs past the chunk stream", (unsigned long)offset);
            break;
        }

        // Half track index is the low 7 bits; bit 7 selects the side. The table
        // is keyed on the RAW byte so both sides can live in it - a 1541 image
        // only ever has side 0, a 1581 image has both - and chunkKeyFor() is
        // what decides which key a track reads from.
        if (chunk[0] == 'H' && chunk[1] == 'T' && chunk[2] == 'P' && chunk_size >= 8)
        {
            uint8_t half_track = chunk[3] & 0x7f;
            if (half_track >= P64_FIRST_HALF_TRACK && half_track <= P64_LAST_HALF_TRACK)
            {
                uint8_t info[8];
                if (read_at(cs, data_start, info, sizeof(info)))
                {
                    HalfTrack entry;
                    entry.pulses = le32(info);
                    entry.size = le32(info + 4);
                    entry.offset = data_start + 8;

                    if (entry.pulses > 0 && entry.size > 0 && entry.size == chunk_size - 8)
                    {
                        half_tracks[chunk[3]] = entry;
                    }
                    else if (entry.size != chunk_size - 8)
                    {
                        Debug_printv("half track [%d] size [%lu] does not fill its chunk [%lu]",
                                     half_track, (unsigned long)entry.size,
                                     (unsigned long)(chunk_size - 8));
                    }
                }
            }
        }

        offset = data_start + chunk_size;
    }

    if (half_tracks.empty())
    {
        Debug_printv("no readable half track data in P64 image");
        return false;
    }

    // The track range is deliberately left at the D64 default of 35, even
    // though the chunk table says exactly which tracks the image carries. A
    // 1541 BAM holds 35 four-byte records and nothing more, so widening the
    // range makes getBAMRecord() read the disk name as free-block counts - and
    // an image with tracks past 35 is usually a protected release that put
    // protection data there, which is precisely when a listing must not start
    // reporting nonsense. Reaching those tracks needs an extended-BAM layout,
    // the same open problem the D64 constructor has for a 40-track .d64.

    chunks_ok = true;
    return true;
}

bool P64MStream::decodeTrack(uint8_t track)
{
    if (cached_track == (int)track)
        return true;

    // resetEmitState() is handed the track because the 1541 read logic needs
    // its speed zone; the cache id is the track, since that is what the caller
    // asks for again.
    return decodeChunk(chunkKeyFor(track), (int)track);
}

bool P64MStream::decodeChunk(uint8_t key, int cache_id)
{
    const uint8_t track = (uint8_t)(cache_id < 0 ? 0 : cache_id);

    auto it = half_tracks.find(key);
    if (it == half_tracks.end())
    {
        Debug_printv("no pulse data for track[%d] (chunk key %02X)", track, key);
        return false;
    }
    const HalfTrack entry = it->second;

    // Bits are OR-ed into the buffer, so it has to start clear - and it holds
    // the previous track until this succeeds, which would otherwise survive in
    // the wrap-around region a sync scan reads.
    gcr_track.assign(trackBufferBytes(), 0);
    gcr_track_bytes = 0;
    cached_track = -1;
    logged_track_mismatch = false;

    uint8_t *compressed = (uint8_t *)malloc(entry.size);
    if (compressed == nullptr)
    {
        Debug_printv("no memory for the %lu byte pulse chunk of track[%d]",
                     (unsigned long)entry.size, track);
        return false;
    }

    if (!read_at(containerStream.get(), entry.offset, compressed, entry.size))
    {
        Debug_printv("cannot read the pulse chunk of track[%d]", track);
        free(compressed);
        return false;
    }

    uint16_t *probabilities = (uint16_t *)malloc(model_total * sizeof(uint16_t));
    if (probabilities == nullptr)
    {
        Debug_printv("no memory for the %lu KB probability model - P64 needs PSRAM",
                     (unsigned long)((model_total * sizeof(uint16_t)) >> 10));
        free(compressed);
        return false;
    }
    // Pulse -> GCR, streamed. The reference materializes the whole pulse list
    // first and then walks it; a track's worth of TP64Pulse is around half a
    // megabyte, and every pulse is consumed in exactly the order it decodes
    // (positions only ever move forward), so the two passes are folded into
    // one and the list never exists.
    const uint32_t buffer_bits = (uint32_t)gcr_track.size() * 8;

    RangeDecoder rc;

    uint32_t position = 0;
    uint32_t delta_position = 0;
    uint32_t strength = 0;
    uint32_t count = 0;

    resetEmitState(track);
    emit_target_bits = buffer_bits;

    // A P64 holds ONE rotation, and its position 0 is wherever the imaging
    // hardware happened to start - not a gap. A sector that straddles that
    // point has its header or data split across the two ends of the bitstream,
    // and the two ends do not join cleanly: the bit cell phase at position 0 is
    // unrelated to the phase where the rotation ran out, and the final cell is
    // truncated. Wrapping the sync scan is therefore not enough - the straddling
    // sector decodes to garbage, or its header is never found at all. Measured
    // on wheels64_4.4a: one bad sector on each of tracks 18, 19, 20 and 23, each
    // with its data block starting past 95% of the rotation.
    //
    // So the rotation is decoded, and then the pulse stream is REPLAYED with
    // every position shifted a whole rotation later, continuing the same read
    // logic state - which is exactly what the drive head sees on the next
    // revolution. That makes the straddling sector contiguous in the overlap.
    // The replay has to restart the range decoder from the beginning of the
    // chunk with fresh models, because they adapt as they decode and cannot be
    // seeked into; it stops after the overlap, so it costs a small fraction of
    // the first pass.
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            if (emit_bit_pos >= buffer_bits)
                break;  // buffer filled by the rotation alone, no room to overlap

            emit_target_bits = emit_bit_pos + (overlapBytes() * 8);
            if (emit_target_bits > buffer_bits)
                emit_target_bits = buffer_bits;

            position = P64_SAMPLES_PER_ROTATION;
            delta_position = 0;
            strength = 0;
            count = 0;
        }

        for (uint32_t i = 0; i < model_total; i++)
            probabilities[i] = 2048;
        rc.start(compressed, entry.size, probabilities);

        while (count < entry.pulses && emit_bit_pos < emit_target_bits)
        {
            if (rc.readFlag(MODEL_POSITION_FLAG))
            {
                delta_position = rc.readDWord(MODEL_POSITION);
                if (delta_position == 0)
                    break; // track stream end marker
            }

            if (delta_position > P64_SAMPLES_PER_ROTATION)
            {
                Debug_printv("track[%d] pulse %lu jumps %lu samples - corrupt chunk",
                             track, (unsigned long)count, (unsigned long)delta_position);
                break;
            }

            position += delta_position;

            if (rc.readFlag(MODEL_STRENGTH_FLAG))
                strength += rc.readDWord(MODEL_STRENGTH);

            count++;

            // Only a strong pulse triggers the read logic. A weak one does not
            // even move the reference point, so it must not touch last_position.
            if (strength < 0x80000000UL)
                continue;

            uint32_t delta = position - emit_last_position;
            emit_last_position = position;

            emitDelta(delta);
        }
    }

    free(probabilities);
    free(compressed);

    if (emit_bit_pos == 0)
    {
        Debug_printv("track[%d] decoded to an empty bitstream", track);
        return false;
    }

    gcr_track_bytes = (emit_bit_pos + 7) >> 3;
    cached_track = cache_id;

    //Debug_printv("track[%d] pulses[%lu] bits[%lu]", track, (unsigned long)count, (unsigned long)emit_bit_pos);
    return true;
}

// The 1541's read logic: a clock divided by the track's speed zone, advancing a
// counter that emits one bit every four counts. Turning flux timing into bits
// this way is what produces a GCR bitstream, and it is the half of the decoder
// that a 1581 replaces wholesale - see P81MStream::emitDelta().
void P64MStream::resetEmitState(uint8_t track)
{
    emit_last_position = 0;
    emit_flip_flop = 0;
    emit_last_flip_flop = 0;
    emit_zone = speedZone(track);
    emit_clock = emit_zone;
    emit_counter = 0;
    emit_bit_pos = 0;
}

void P64MStream::emitDelta(uint32_t delta)
{
    uint32_t delay = 0;
    emit_flip_flop ^= 1;
    do
    {
        if ((delay == 40) && (emit_last_flip_flop != emit_flip_flop))
        {
            emit_last_flip_flop = emit_flip_flop;
            emit_clock = emit_zone;
            emit_counter = 0;
        }
        if (emit_clock == 16)
        {
            emit_clock = emit_zone;
            emit_counter = (emit_counter + 1) & 0x0f;
            if ((emit_counter & 3) == 2)
            {
                gcr_track[emit_bit_pos >> 3] |=
                    (uint8_t)((((emit_counter + 0x1c) >> 4) & 1) << ((~emit_bit_pos) & 7));
                if (++emit_bit_pos >= emit_target_bits)
                    break;
            }
        }
        emit_clock++;
    } while (++delay < delta);
}

int P64MStream::findSync(int p, int limit) const
{
    if (gcr_track_bytes == 0)
        return -1;

    const int bits = (int)gcr_track_bytes * 8;
    if (p < 0 || p >= bits)
        p = 0;

    uint32_t w = 0;
    int b = gcr_track[p >> 3] << (p & 7);

    while (limit--)
    {
        if (b & 0x80)
        {
            w = (w << 1) | 1;
        }
        else
        {
            // Ten 1 bits in a row is a sync; p is then the first bit after it.
            if (~w & 0x3ff)
                w <<= 1;
            else
                return p;
        }

        if (~p & 7)
        {
            p++;
            b <<= 1;
        }
        else
        {
            p++;
            if (p >= bits)
                p = 0;
            b = gcr_track[p >> 3];
        }
    }

    return -1;
}

void P64MStream::decodeBlock(int p, uint8_t *buf, int groups) const
{
    const uint8_t *data = gcr_track.data();
    const uint8_t *end = data + gcr_track_bytes;
    const int shift = p & 7;
    const uint8_t *offset = data + (p >> 3);

    uint8_t gcr[5];
    uint8_t b = (uint8_t)(offset[0] << shift);

    for (int i = 0; i < groups; i++, buf += 4)
    {
        for (int j = 0; j < 5; j++)
        {
            offset++;
            if (offset >= end)
                offset = data;

            if (shift)
            {
                gcr[j] = (uint8_t)(b | ((offset[0] << shift) >> 8));
                b = (uint8_t)(offset[0] << shift);
            }
            else
            {
                gcr[j] = b;
                b = offset[0];
            }
        }
        gcr_to_bytes(gcr, buf);
    }
}

bool P64MStream::loadSector(uint8_t track, uint8_t sector)
{
    const int bits = (int)gcr_track_bytes * 8;

    uint8_t header[8];
    uint8_t block[260];

    int p = 0;
    int first = -1;

    // A track carries at most 21 sectors, but a gap can hold a false sync, so
    // the scan is bounded by having come all the way round rather than by a
    // sector count. The iteration cap is a backstop against a bitstream where
    // that never happens.
    for (int guard = 0; guard < 256; guard++)
    {
        p = findSync(p, bits);
        if (p < 0)
            break;
        if (p == first)
            break; // back where the scan started - the whole track is read
        if (first < 0)
            first = p;

        decodeBlock(p, header, 2);
        if (header[0] != 0x08)
            continue; // not a sector header block

        uint8_t checksum = header[1] ^ header[2] ^ header[3] ^ header[4] ^ header[5];
        if (checksum != 0)
            continue; // false sync, or a header this drive cannot read

        if (header[2] != sector)
            continue;

        if (header[3] != track && !logged_track_mismatch)
        {
            // Worth saying out loud: it means the half track this decoded is
            // not the track the header claims to be on. Once per decoded track.
            logged_track_mismatch = true;
            Debug_printv("track[%d] sector[%d] header says track[%d]", track, sector, header[3]);
        }

        // The data block follows its own sync, within a header gap's distance.
        int d = findSync(p, 500 * 8);
        if (d < 0)
        {
            Debug_printv("no data block after header for track[%d] sector[%d]", track, sector);
            return false;
        }

        decodeBlock(d, block, 65);
        if (block[0] != 0x07)
        {
            Debug_printv("track[%d] sector[%d] data block id [%02X]", track, sector, block[0]);
            return false;
        }

        uint8_t data_checksum = block[257];
        for (int i = 0; i < 256; i++)
            data_checksum ^= block[i + 1];
        last_data_checksum_ok = (data_checksum == 0);
        if (data_checksum != 0)
        {
            // Reported, not refused: an original disk can carry deliberately
            // bad checksums, and the caller has no error channel to hear about
            // it on. The bytes are the drive's best read either way.
            Debug_printv("track[%d] sector[%d] data checksum error", track, sector);
        }

        std::memcpy(sector_buffer, block + 1, sizeof(sector_buffer));
        return true;
    }

    Debug_printv("track[%d] sector[%d] not found", track, sector);
    return false;
}

bool P64MStream::seekSector(uint8_t track, uint8_t sector, uint8_t offset)
{
    // Is this a valid track?
    uint16_t c = curPartition().block_allocation_map.size() - 1;
    uint8_t start_track = curPartition().block_allocation_map[0].start_track;
    uint8_t end_track = curPartition().block_allocation_map[c].end_track;
    if (track < start_track || track > end_track)
    {
        Debug_printv("Invalid Track: track[%d] start_track[%d] end_track[%d]", track, start_track, end_track);
        return false;
    }

    // Is this a valid sector?
    c = getSectorCount(track);
    if (sector >= c)
    {
        Debug_printv("Invalid Sector: sector[%d] sectorsPerTrack[%d]", sector, c);
        return false;
    }

    if (!parseChunks())
        return false;

    if (!decodeTrack(track))
        return false;

    if (!loadSector(track, sector))
        return false;

    // Block number is the same linear count a .d64 would give, so anything
    // reporting a block number stays comparable across the two formats.
    uint16_t sectorOffset = 0;
    for (uint8_t index = 1; index < track; ++index)
        sectorOffset += getSectorCount(index);
    sectorOffset += sector;

    this->block = sectorOffset;
    this->track = track;
    this->sector = sector;
    sector_pos = offset;

    return true;
}

uint32_t P64MStream::readContainer(uint8_t *buf, uint32_t size)
{
    // The sector is already decoded in RAM; reads walk it the way the D64 layer
    // walks the container after a seek, so the cursor lives here and not in
    // _position (which is the position in the FILE being read, not the sector).
    if (sector_pos >= sizeof(sector_buffer))
        return 0;

    uint32_t remaining = (uint32_t)sizeof(sector_buffer) - sector_pos;
    if (size > remaining)
        size = remaining;

    std::memcpy(buf, sector_buffer + sector_pos, size);
    sector_pos += size;
    return size;
}
