// .ARK - ARKive containers
// .SRK - Compressed ARKive archives (very rare)
//
// https://ist.uwaterloo.ca/~schepers/formats/ARK-SRK.TXT
// https://www.zimmers.net/anonftp/pub/cbm/crossplatform/converters/unix/unkar.c
//


#ifndef MEATLOAF_MEDIA_ARK
#define MEATLOAF_MEDIA_ARK

#include "../meatloaf.h"
#include "../meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class ARKMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    ARKMStream(std::shared_ptr<MStream> is) : MMediaStream(is)
    {
        // Get the entry count
        containerStream->seek(0);
        uint8_t count;
        readContainer((uint8_t *)&count, 1);
        entry_count = count;
    };

protected:
    struct Header {
        std::string disk_name;
        std::string id_dos;
    };

    struct __attribute__ ((__packed__)) Entry {
        uint8_t file_type;
        uint8_t lsu_byte;           // Last Sector Usage (bytes used in the last sector)
        char filename[16];
        uint8_t rel_record_length;  // Or GEOS file structure (Sequential / VLIR file)
        uint8_t geos_file_type;     // $00 - Non-GEOS (normal C64 file)
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        uint8_t minute;
        uint8_t rel_block_count;    // REL file side sector block count (side sector info contained at end of file)
        uint8_t rel_lsu;            // Number of bytes+1 used in the last side sector entry
        uint16_t blocks;
    };

    bool readHeader() override { return true; };
    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };
    bool seekPath(std::string path) override;

    Header header;
    Entry entry;

private:
    friend class ARKMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class ARKMFile: public MFile {
public:

    ARKMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~ARKMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new ARKMStream(containerIstream);
    }

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
    //uint32_t size() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class ARKMFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new ARKMFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".ark", fileName);
    }

    ARKMFileSystem(): MFileSystem("ark") {};
};


#endif /* MEATLOAF_MEDIA_ARK */
