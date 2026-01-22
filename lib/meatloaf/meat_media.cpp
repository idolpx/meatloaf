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

#include "meat_media.h"
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

std::unordered_map<std::string, std::shared_ptr<MMediaStream>>ImageBroker::image_repo;

// Utility Functions

std::string MMediaStream::decodeType(uint8_t file_type, bool show_hidden)
{
    //bool hidden = false;
    std::string type = "";

    // Check if splat file
    // Bit 7: Closed flag  (Not  set  produces  "*", or "splat" files)
    if (!(file_type >> 7 & 1)) {
        type += "*";
        //hidden = true;
    } else {
        type += " ";
    }

    type += file_type_label[ file_type & 0b00000111 ];
    //if ( file_type == 0 )
    //    hidden = true;

    // Bit 6: Locked flag (Set produces ">" locked files)
    if ((file_type >> 6 & 1)) {
        type += "<";
        //hidden = false;
    } else {
        type += " ";
    }

    return type;
}

std::string MMediaStream::decodeType(std::string file_type)
{
    std::string type = " PRG";

    if (file_type == "P")
        type = " PRG";
    else if (file_type == "S")
        type = " SEQ";
    else if (file_type == "U")
        type = " USR";
    else if (file_type == "R")
        type = " REL";

    return type;
}

std::string MMediaStream::decodeGEOSType(uint8_t geos_file_structure, uint8_t geos_file_type)
{
    // Decode geos_file_type
    std::string geos_type;
    if ( geos_file_type )
    {
        if ( geos_file_structure == 0x00 )
            geos_type = "SEQ ";
        else
            geos_type = "VLIR ";

        switch (geos_file_type) {
            case 0x01:
                geos_type += "BASIC";
                break;

            case 0x02:
                geos_type += "Assembler";
                break;

            case 0x03:
                geos_type += "Data file";
                break;

            case 0x04:
                geos_type += "System File";
                break;

            case 0x05:
                geos_type += "Desk Accessory";
                break;

            case 0x06:
                geos_type += "Application";
                break;

            case 0x07:
                geos_type += "Application Data"; // (user-created documents)
                break;

            case 0x08:
                geos_type += "Font File";
                break;

            case 0x09:
                geos_type += "Printer Driver";
                break;

            case 0x0A:
                geos_type += "Input Driver";
                break;

            case 0x0B:
                geos_type += "Disk Driver"; // (or Disk Device)
                break;

            case 0x0C:
                geos_type += "System Boot File";
                break;

            case 0x0D:
                geos_type += "Temporary";
                break;

            case 0x0E:
                geos_type += "Auto-Execute File";
                break;
            
            default:
                geos_type += "Undefined";
        }
    }
    return geos_type;
}

/********************************************************
 * Istream impls
 ********************************************************/

// std::string MMediaStream::seekNextEntry() {
//     // Implement this to skip a queue of file streams to start of next file and return its name
//     // this will cause the next read to return bytes of "next" file in D64 image
//     // might not have sense in this case, as D64 is kinda random access, not a stream.
//     return "";
// };

bool MMediaStream::isOpen() {

    return _is_open;
};



bool MMediaStream::open(std::ios_base::openmode mode) 
{
    // return true if we were able to read the image and confirmed it is valid.
    // it's up to you in what state the stream will be after open. Could be either:
    // 1. EOF-like state (0 available) and the state will be cleared only after succesful seekNextEntry or seekPath
    // 2. non-EOF-like state, and ready to send bytes of first file, because you did immediate seekNextEntry here

    return false;
};

void MMediaStream::close()
{
    Debug_memory();
}


uint32_t MMediaStream::readContainer(uint8_t *buf, uint32_t size)
{
    //Debug_printv("readContainer[%lu]", size);
    uint32_t bytesRead = containerStream->read(buf, size);
    if (bytesRead < size) {
        Debug_printv("WARNING: Short read - requested %lu, got %lu", size, bytesRead);
    }
    return bytesRead;
}
uint32_t MMediaStream::writeContainer(uint8_t *buf, uint32_t size)
{
    //Debug_printv("writeContainer[%lu]", size);
    return containerStream->write(buf, size);
}

uint8_t MMediaStream::read() 
{
    uint8_t b = 0;
    containerStream->read( &b, 1 );
    _position++;
    return b;
}

uint32_t MMediaStream::read(uint8_t* buf, uint32_t size) {
    uint32_t bytesRead = 0;

    //Debug_printv("read[%lu] seekCalled[%d]", size, seekCalled);
    if ( _position >= _size )
        return 0;

    if(seekCalled) {
        // if we have the stream set to a specific file already, either via seekNextEntry or seekPath, return bytes of the file here
        // or set the stream to EOF-like state, if whle file is completely read.
        bytesRead = readFile(buf, size);

    }
    else {
        // seekXXX not called - just pipe image bytes, so it can be i.e. copied verbatim
        bytesRead = readContainer(buf, size);
    }

    _position += bytesRead;

    return bytesRead;
};
// readUntil = (delimiter = 0x00) => this.containerStream.readUntil(delimiter);
std::string MMediaStream::readUntil( uint8_t delimiter )
{
    uint8_t b = 0, s = 0;
    std::string bytes = "";
    do
    {
        s = containerStream->read( &b, 1 );
        _position += s;
        if ( b != delimiter )
        {
            bytes += b;
        }
        else
            break;
    } while ( s );

    return bytes;
}
// readString = (size) => this.containerStream.readString(size);
std::string MMediaStream::readString( uint8_t size )
{
    uint8_t b[size];
    if ( auto s = containerStream->read( b, size ) )
    {
        _position += s;
        return std::string((char *)b);
    }
    return std::string();
}
// readStringUntil = (delimiter = 0x00) => this.containerStream.readStringUntil(delimiter);
std::string MMediaStream::readStringUntil( uint8_t delimiter )
{
    uint8_t b = 0;
    std::stringstream ss;
    while( containerStream->read( &b, 1 ) )
    {
        _position++;
        if ( b == delimiter )
            ss << b;
    }
    return ss.str();
}

uint32_t MMediaStream::write(const uint8_t *buf, uint32_t size) {
    return containerStream->write(buf, size);
}

// seek = (offset) => this.containerStream.seek(offset + this.media_header_size);
bool MMediaStream::seek(uint32_t offset) {
    _position = media_data_offset + offset;
    return containerStream->seek( _position ); 
}
// seekCurrent = (offset) => this.containerStream.seekCurrent(offset);
bool MMediaStream::seekCurrent(uint32_t offset) {
    _position += offset;
    return containerStream->seek( _position );
}

uint32_t MMediaStream::seekFileSize( uint8_t start_track, uint8_t start_sector )
{
    // Calculate file size
    Debug_print("Calculating file size...\r\n");
    seekSector(start_track, start_sector);

    size_t blocks = 0; 
    const size_t MAX_BLOCKS = 10000;  // Safety limit
    do
    {
        // This causes watchdog resets to be missed on long files. Leave commented unless used for debugging.
        //console.printf("t[%d] s[%d] b[%d]\r", start_track, start_sector, blocks);
        
        // Safety check for runaway loops
        if (blocks >= MAX_BLOCKS) {
            Debug_printv("ERROR: Block chain too long (>%d blocks), aborting", MAX_BLOCKS);
            break;
        }
        
        // Validate containerStream is still valid before reading
        if (!containerStream) {
            Debug_printv("FATAL: containerStream became NULL at block %d!", blocks);
            break;
        }
        
        // Read track and sector link - MUST check return values!
        uint32_t track_bytes = readContainer(&start_track, 1);
        uint32_t sector_bytes = readContainer(&start_sector, 1);
        
        // If we couldn't read the link bytes, the stream has failed
        if (track_bytes != 1 || sector_bytes != 1) {
            Debug_printv("ERROR: Failed to read block chain link at block %d (read %lu/%lu bytes)", 
                        blocks, track_bytes, sector_bytes);
            // Stream error - best to abort here
            break;
        }
        
        blocks++;
        
        // Yield to other tasks every 10 blocks to prevent watchdog timeout
        // and feed watchdog every 100 blocks
        if (blocks % 10 == 0) {
            vTaskDelay(1);  // Yield to scheduler
            if (blocks % 100 == 0) {
                // Try to reset watchdog, ignore errors if task not subscribed
                esp_err_t err = esp_task_wdt_reset();
                if (err != ESP_OK && blocks == 100) {
                    Debug_printv("Note: Task not subscribed to watchdog (this is OK)");
                }
            }
        }
        
        if ( start_track > 0 )
            if ( !seekSector( start_track, start_sector ) )
                break;
    } while ( start_track > 0 );
    blocks--;
    uint32_t size = (blocks * (block_size - 2)) + start_sector - 1;
    console.printf("File size is [%lu] bytes...\r\n", size);
    return size;
};