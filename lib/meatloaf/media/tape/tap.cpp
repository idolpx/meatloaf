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

#include "tap.h"

#include "meat_broker.h"
#include "endianness.h"
#include <cstring>

/********************************************************
 * Constants (from wav-prg kernal loader)
 ********************************************************/

// Pulse duration thresholds (in C64 machine cycles * 8)
// Pulse type 0: < 426
// Pulse type 1: 426-616
// Pulse type 2: > 616
const uint16_t TAPMStream::kernal_thresholds[2] = {426, 616};

// Standard C64 tape pilot sequence (decreasing countdown pattern)
const uint8_t TAPMStream::kernal_pilot_sequence[9] = {137, 136, 135, 134, 133, 132, 131, 130, 129};

/********************************************************
 * Streams
 ********************************************************/

bool TAPMStream::readHeader()
{
    // Read TAP header (20 bytes)
    containerStream->seek(0);
    if (containerStream->read((uint8_t*)&tap_header, sizeof(TAPHeader)) != sizeof(TAPHeader))
    {
        Debug_printv("Failed to read TAP header");
        return false;
    }

    // Validate signature
    if (strncmp(tap_header.signature, "C64-TAPE-RAW", 12) != 0)
    {
        Debug_printv("Invalid TAP signature: %.12s", tap_header.signature);
        return false;
    }

    // Store header info
    header.signature = std::string(tap_header.signature, 12);
    header.version = tap_header.version;
    header.data_size = UINT32_FROM_LE_UINT32(tap_header.data_size);

    pulse_data_start = sizeof(TAPHeader);

    Debug_printv("TAP Format: signature[%.12s] version[%d] data_size[%d]",
        tap_header.signature, tap_header.version, header.data_size);

    return true;
}

void TAPMStream::analyzeTapeData()
{
    // Reset position to start of pulse data
    tap_position = pulse_data_start;

    // Build index of files on tape (fast - no data decoding)
    Debug_printv("Indexing TAP files, pulse data size: %d bytes", header.data_size);

    while (tap_position < pulse_data_start + header.data_size)
    {
        // Try to find sync sequence
        if (!findSync())
        {
            Debug_printv("No more sync sequences found at position %d", tap_position);
            break;
        }

        // Try to read tape header
        uint8_t file_type;
        std::string filename;
        uint16_t start_addr, end_addr;

        if (!readTapeHeader(file_type, filename, start_addr, end_addr))
        {
            Debug_printv("Failed to read tape header at position %d", tap_position);
            continue;  // Try to find next file
        }

        // Calculate expected data size
        uint16_t data_size = (end_addr >= start_addr) ? (end_addr - start_addr) : 0;

        // Try to find data block sync
        if (!findSync())
        {
            Debug_printv("Failed to find data block sync");
            continue;
        }

        // Skip over data block and record its position
        uint16_t bytes_length = 0;
        uint32_t data_offset = 0;

        if (!skipDataBlock(data_size, bytes_length, data_offset))
        {
            Debug_printv("Failed to skip data block");
            continue;
        }

        // Create tape file index entry (no data cached)
        TapeFile file;
        file.filename = filename;
        file.file_type = file_type;
        file.data_offset = data_offset;  // Position in TAP where data starts
        file.data_length = bytes_length;
        file.start_address = start_addr;
        file.end_address = start_addr + bytes_length;
        // Note: cached_data is empty - data will be decoded on-demand

        tape_files.push_back(file);

        Debug_printv("Indexed file: '%s' Type:%02X Offset:%u Length:%u Addr:%04X-%04X",
            filename.c_str(), file_type, data_offset, bytes_length, start_addr, file.end_address);
    }

    if (tape_files.empty())
    {
        // Fallback: provide access to raw TAP data
        Debug_printv("No files indexed, providing raw TAP access");

        TapeFile raw_file;
        raw_file.filename = "RAW.TAP";
        raw_file.file_type = 0x00;
        raw_file.data_offset = pulse_data_start;
        raw_file.data_length = header.data_size;
        raw_file.start_address = 0;
        raw_file.end_address = header.data_size;

        tape_files.push_back(raw_file);
    }
    else
    {
        Debug_printv("Successfully indexed %d file(s) from tape", tape_files.size());
    }
}

/********************************************************
 * TAP Pulse Decoding Functions (adapted from wav-prg)
 ********************************************************/

// Read one pulse from TAP file (from libaudiotap tapfile_get_pulse)
bool TAPMStream::readTAPPulse(uint32_t& pulse)
{
    if (tap_position >= pulse_data_start + header.data_size)
        return false;

    containerStream->seek(tap_position);

    uint8_t byte;
    if (containerStream->read(&byte, 1) != 1)
        return false;

    tap_position++;

    if (byte != 0)
    {
        // Short pulse: value * 8
        pulse = byte * 8;
        return true;
    }

    // Long pulse (TAP version 1): read 3-byte value
    if (header.version == 0)
    {
        // TAP version 0: byte 0 means special long pulse
        pulse = 1000000;
        return true;
    }

    uint8_t threebytes[3];
    if (containerStream->read(threebytes, 3) != 3)
        return false;

    tap_position += 3;

    pulse = threebytes[0] | (threebytes[1] << 8) | (threebytes[2] << 16);
    return true;
}

// Convert two pulses to a bit (from kernal_get_bit_func)
bool TAPMStream::pulseToBit(uint8_t pulse1, uint8_t pulse2, uint8_t& bit)
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

// Get one bit from pulse stream
bool TAPMStream::getPulseBit(uint8_t& bit)
{
    uint32_t pulse1_raw, pulse2_raw;

    if (!readTAPPulse(pulse1_raw))
        return false;
    if (!readTAPPulse(pulse2_raw))
        return false;

    // Convert raw pulse durations to pulse types using thresholds
    uint8_t pulse1, pulse2;

    if (pulse1_raw < kernal_thresholds[0])
        pulse1 = 0;
    else if (pulse1_raw < kernal_thresholds[1])
        pulse1 = 1;
    else
        pulse1 = 2;

    if (pulse2_raw < kernal_thresholds[0])
        pulse2 = 0;
    else if (pulse2_raw < kernal_thresholds[1])
        pulse2 = 1;
    else
        pulse2 = 2;

    return pulseToBit(pulse1, pulse2, bit);
}

// Read one byte from bit stream (8 bits, LSB first)
bool TAPMStream::getByte(uint8_t& byte)
{
    byte = 0;

    for (int i = 0; i < 8; i++)
    {
        uint8_t bit;
        if (!getPulseBit(bit))
            return false;

        // LSB first (lsbf endianness)
        byte = (byte >> 1) | (bit << 7);
    }

    return true;
}

// Read byte with sync and parity check (from sync_with_byte_and_get_it)
bool TAPMStream::getByteWithSync(uint8_t& byte, bool allow_short_first)
{
    uint32_t pulse_raw;
    uint8_t pulse;

    // Look for sync pulse (pulse type 2 - long pulse)
    while (true)
    {
        if (!readTAPPulse(pulse_raw))
            return false;

        // Convert to pulse type
        if (pulse_raw < kernal_thresholds[0])
            pulse = 0;
        else if (pulse_raw < kernal_thresholds[1])
            pulse = 1;
        else
            pulse = 2;

        if (pulse == 2)
            break;

        if (pulse != 0 || !allow_short_first)
            return false;
    }

    // Next pulse should be type 1 (medium)
    if (!readTAPPulse(pulse_raw))
        return false;

    if (pulse_raw < kernal_thresholds[0])
        pulse = 0;
    else if (pulse_raw < kernal_thresholds[1])
        pulse = 1;
    else
        pulse = 2;

    if (pulse == 0)
    {
        // EOF marker
        return false;
    }

    if (pulse != 1)
        return false;

    // Read the actual byte
    if (!getByte(byte))
        return false;

    // Read and verify parity bit
    uint8_t parity;
    if (!getPulseBit(parity))
        return false;

    // Calculate parity
    uint8_t expected_parity = 0;
    for (uint8_t test = 1; test; test <<= 1)
        expected_parity ^= (byte & test) ? 1 : 0;

    return (parity == expected_parity);
}

// Find sync sequence (pilot tone followed by sync byte)
bool TAPMStream::findSync()
{
    const int max_search = 100000;  // Limit search to avoid infinite loops
    int search_count = 0;

    while (search_count++ < max_search)
    {
        uint8_t byte;
        if (getByteWithSync(byte, true))
        {
            // Successfully found a sync byte
            return true;
        }

        // Skip forward a bit and try again
        tap_position += 1;
    }

    return false;
}

// Read tape header (21 bytes at address 828-848)
bool TAPMStream::readTapeHeader(uint8_t& file_type, std::string& filename, uint16_t& start_addr, uint16_t& end_addr)
{
    uint8_t header_data[21];

    // Read 21 bytes of header
    for (int i = 0; i < 21; i++)
    {
        if (!getByteWithSync(header_data[i], false))
        {
            Debug_printv("Failed to read header byte %d", i);
            return false;
        }
    }

    // Parse header structure:
    // Byte 0: File type (1 = relocatable, 3 = non-relocatable)
    // Bytes 1-2: Start address (little-endian)
    // Bytes 3-4: End address (little-endian)
    // Bytes 5-20: Filename (16 bytes, PETSCII)

    file_type = header_data[0];

    if (file_type != 1 && file_type != 3)
    {
        Debug_printv("Invalid file type: %02X", file_type);
        return false;
    }

    start_addr = header_data[1] | (header_data[2] << 8);
    end_addr = header_data[3] | (header_data[4] << 8);

    // Extract filename (16 bytes, null-terminated or space-padded)
    filename.assign((char*)&header_data[5], 16);

    // Trim trailing spaces and nulls
    while (!filename.empty() && (filename.back() == ' ' || filename.back() == '\0'))
        filename.pop_back();

    return true;
}

// Read data block with checksum
bool TAPMStream::readDataBlock(uint8_t* buffer, uint16_t max_size, uint16_t& bytes_read)
{
    bytes_read = 0;
    uint8_t checksum = 0;

    // Read data bytes until EOF marker or max size
    while (bytes_read < max_size)
    {
        uint8_t byte;
        if (!getByteWithSync(byte, false))
        {
            // Could be EOF marker or end of block
            break;
        }

        buffer[bytes_read++] = byte;
        checksum ^= byte;  // XOR checksum
    }

    // Try to read checksum byte
    uint8_t expected_checksum;
    if (getByteWithSync(expected_checksum, false))
    {
        if (checksum != expected_checksum)
        {
            Debug_printv("Checksum mismatch: got %02X, expected %02X", checksum, expected_checksum);
            // Continue anyway - data might still be usable
        }
    }

    return bytes_read > 0;
}

// Skip over data block without reading it (for fast indexing)
bool TAPMStream::skipDataBlock(uint16_t max_size, uint16_t& bytes_skipped, uint32_t& data_start_position)
{
    bytes_skipped = 0;
    data_start_position = tap_position;

    // Skip data bytes until EOF marker or max size
    while (bytes_skipped < max_size)
    {
        uint8_t byte;
        if (!getByteWithSync(byte, false))
        {
            // Could be EOF marker or end of block
            break;
        }

        bytes_skipped++;
    }

    // Try to skip checksum byte
    uint8_t checksum_byte;
    getByteWithSync(checksum_byte, false);

    return bytes_skipped > 0;
}

std::string TAPMStream::seekNextEntry()
{
    // Return next file from tape
    if (current_file_index >= tape_files.size())
    {
        current_file_index = 0;  // Reset for next iteration
        return "";  // No more entries
    }

    // Get current entry
    TapeFile& file = tape_files[current_file_index];

    entry.filename = file.filename;
    entry.file_type = file.file_type;
    entry.data_offset = file.data_offset;
    entry.data_length = file.data_length;
    entry.start_address = file.start_address;
    entry.end_address = file.end_address;

    // Set stream size and position for this entry
    _size = entry.data_length;
    _position = 0;

    // Position container stream at start of data
    containerStream->seek(entry.data_offset);

    Debug_printv("Entry[%d]: %s Type:%02X Offset:%d Length:%d",
        current_file_index, entry.filename.c_str(), entry.file_type,
        entry.data_offset, entry.data_length);

    current_file_index++;

    return entry.filename;
}

bool TAPMStream::seekPath(std::string path)
{
    // TODO: Implement random access seeking if IDX file present
    return false;
}

bool TAPMStream::seek(uint32_t pos)
{
    // For sequential tape, seeking is limited
    // Can only seek within current file entry
    if (pos > _size)
        pos = _size;

    _position = pos;

    // Seek in container stream relative to current entry
    return containerStream->seek(entry.data_offset + pos);
}

uint32_t TAPMStream::read(uint8_t* buf, uint32_t size)
{
    // Limit read to remaining file size
    if (_position + size > _size)
    {
        size = _size - _position;
    }

    if (size == 0)
        return 0;

    // Check if we have a current file
    if (current_file_index > 0 && current_file_index <= tape_files.size())
    {
        TapeFile& file = tape_files[current_file_index - 1];

        // If data not cached yet, decode it on-demand
        if (file.cached_data.empty() && file.data_length > 0 && file.data_length < 65536)
        {
            Debug_printv("Decoding file data on-demand: %s (%d bytes)", 
                file.filename.c_str(), file.data_length);
            
            // Save current position and seek to file data
            uint32_t saved_position = tap_position;
            tap_position = file.data_offset;
            
            // Decode the data
            uint8_t temp_buffer[65536];
            uint16_t bytes_read = 0;
            
            if (readDataBlock(temp_buffer, file.data_length, bytes_read))
            {
                // Cache the decoded data
                file.cached_data.assign(temp_buffer, temp_buffer + bytes_read);
                Debug_printv("Cached %d bytes for %s", bytes_read, file.filename.c_str());
            }
            else
            {
                Debug_printv("Failed to decode data on-demand");
            }
            
            // Restore position
            tap_position = saved_position;
        }

        if (!file.cached_data.empty())
        {
            // Read from cached decoded data
            uint32_t available = file.cached_data.size() - _position;
            if (size > available)
                size = available;

            if (size > 0)
            {
                memcpy(buf, file.cached_data.data() + _position, size);
                _position += size;
                return size;
            }

            return 0;
        }
    }

    // Fallback: read raw TAP data from container stream (for RAW.TAP entry)
    containerStream->seek(entry.data_offset + _position);
    uint32_t bytesRead = containerStream->read(buf, size);
    _position += bytesRead;

    return bytesRead;
}

/********************************************************
 * File implementations
 ********************************************************/

std::shared_ptr<MStream> TAPMFile::getDecodedStream(std::shared_ptr<MStream> is)
{
    Debug_printv("[%s]", url.c_str());
    return std::make_shared<TAPMStream>(is);
}

bool TAPMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("url[%s]", url.c_str());

    // Create or reuse cached TAP stream
    if (!cached_stream)
    {
        auto containerStream = sourceFile->getSourceStream();
        if (containerStream == nullptr)
            return false;

        cached_stream = std::make_shared<TAPMStream>(containerStream);
        if (!cached_stream->isOpen())
        {
            cached_stream.reset();
            return false;
        }
    }

    // Reset to beginning of tape
    cached_stream->current_file_index = 0;

    // Set Media Info Fields
    media_header = "C64 TAPE";
    media_id = mstr::format("TAP V%d", cached_stream->header.version);
    media_blocks_free = 0;
    media_block_size = cached_stream->block_size;
    media_image = name;

    Debug_printv("media_header[%s] media_id[%s]",
        media_header.c_str(), media_id.c_str());

    return true;
}

MFile* TAPMFile::getNextFileInDir()
{
    if (!dirIsOpen)
        rewindDirectory();

    if (!cached_stream)
    {
        dirIsOpen = false;
        return nullptr;
    }

    // Get next entry from tape
    std::string filename = cached_stream->seekNextEntry();
    if (filename.empty())
    {
        dirIsOpen = false;
        return nullptr;
    }

    mstr::replaceAll(filename, "/", "\\");

    auto file = MFSOwner::File(url + "/" + filename);
    file->extension = "TAP";
    file->size = cached_stream->entry.data_length;

    Debug_printv("Entry: %s Size:%d", filename.c_str(), file->size);

    return file;
}
