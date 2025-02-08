// .ATR - Atari disk image format
//
// https://github.com/jhallen/atari-tools
//


#ifndef MEATLOAF_MEDIA_ATR
#define MEATLOAF_MEDIA_ATR

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class ATRMStream : public D64MStream {
    // override everything that requires overriding here

public:
    ATRMStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // ATR Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                1,      // track
                0,      // sector
                0x00,   // offset
                1,      // start_track
                40,     // end_track
                4       // byte_count
            }
        };

        Partition p = {
            1,     // track
            0,     // sector
            0x04,  // header_offset
            1,     // directory_track
            4,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 18 };
        
        block_size = 128;
        media_header_size = 0x0F; // 16 byte .atr header
        media_data_offset = 0x0F;

        dos_rom = "a8b-dos20s";

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 92176:  // DOS 2.0s single density
                break;   // 16 byte .atr header + 40 tracks * 18 sectors per track * 128 bytes per sector

            case 133136: // DOS 2.5 enhanced density
                sectorsPerTrack = { 26 };
                break;   // 16 byte .atr header + 40 tracks * 26 sectors per track * 128 bytes per sector

            case 183952: // DOS 2.0d double density
                block_size = 256;
                break;   // 16 byte .atr header + 40 track * 18 sectors per track * 256 bytes per sector - 384 bytes because first three sectors are short
        }
    };

    virtual uint8_t speedZone( uint8_t track) override { return 0; };

protected:

private:
    friend class ATRMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class ATRMFile: public D64MFile {
public:
    ATRMFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) 
    {
        media_block_size = 128;
        size = 92176; // Default - 16 byte .atr header + 40 tracks * 18 sectors per track * 128 bytes per sector
    };

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new ATRMStream(containerIstream);
    }
};



/********************************************************
 * FS
 ********************************************************/

class ATRMFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new ATRMFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".atr", fileName);
    }

    ATRMFileSystem(): MFileSystem("atr") {};
};


#endif /* MEATLOAF_MEDIA_ATR */
