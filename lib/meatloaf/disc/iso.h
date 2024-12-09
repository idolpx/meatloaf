// .ISO - ISO 9660 Optical Disc Image
//
// https://docs.fileformat.com/compression/iso/
// https://en.wikipedia.org/wiki/Optical_disc_image
// https://en.wikipedia.org/wiki/ISO_9660
// https://www.garykessler.net/library/file_sigs.html
//
// https://archive.org/download/tpugusersgroupcd/TPUG%20Users%20Group%20CD/TPUG%20Users%20Group%20CD.iso
// https://archive.org/download/PCC64Emulator/PC%20C64%20Emulator.iso
//

#ifndef MEATLOAF_MEDIA_ISO
#define MEATLOAF_MEDIA_ISO

#include "../meatloaf.h"
#include "../meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class ISOMStream : public MMediaStream {
protected:
	struct EntryDateTime
	{
		uint8_t years_since_1900;
		uint8_t month;
		uint8_t day;
		uint8_t hour;
		uint8_t minute;
		uint8_t second;
		int8_t gmt_offset;
	};

	enum EntryFlags : uint8_t
	{
		Hidden = (1 << 0),
		Directory = (1 << 1),
		AssociatedFile = (1 << 2),
		ExtendedAttributePresent = (1 << 3),
		OwnerGroupPermissions = (1 << 4),
		MoreExtents = (1 << 7),
	};

	struct Entry
	{
		uint8_t entry_length;
		uint8_t extended_attribute_length;
		uint32_t location_le;
		uint32_t location_be;
		uint32_t length_le;
		uint32_t length_be;
		EntryDateTime recoding_time;
		EntryFlags flags;
		uint8_t interleaved_unit_size;
		uint8_t interleaved_gap_size;
		uint16_t sequence_le;
		uint16_t sequence_be;
		uint8_t filename_length;
	};

public:
    bool has_subdirs = true;
    const size_t block_size = 2048;

    ISOMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {


        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 174848: // CD
                break;

            case 1222967296: // DVD
                break;
        }
    };

protected:
    struct Header {
        char disk_name[16];
    };

    struct Entry {
        char filename[16];
        uint8_t file_type;
        uint8_t file_start_address[2]; // from tcrt file system at 0xD8
        uint8_t file_size[3];
        uint8_t file_load_address[2];
        uint16_t bundle_compatibility;
        uint16_t bundle_main_start;
        uint16_t bundle_main_length;
        uint16_t bundle_main_call_address;
    };

    void seekHeader() override {
        containerStream->seek(0x18);
        containerStream->read((uint8_t*)&header, sizeof(header));
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;
    bool seekPath(std::string path) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;

    Header header;
    Entry entry;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

private:
    friend class ISOMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class ISOMFile: public MFile {
public:

    /**
     * Construct an ISOMFile object.
     * @param path The path to the ISO file.
     * @param is_dir Whether the file is a directory or not.
     */
    ISOMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~ISOMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new ISOMStream(containerIstream);
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
    uint32_t size() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class ISOMFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new ISOMFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".tcrt", fileName);
    }

    ISOMFileSystem(): MFileSystem("tcrt") {};
};


#endif /* MEATLOAF_MEDIA_ISO */
