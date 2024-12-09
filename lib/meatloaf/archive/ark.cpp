
#include "ark.h"

//#include "endianness.h"
#include "utils.h"

/********************************************************
 * Streams
 ********************************************************/

bool ARKMStream::seekEntry( std::string filename )
{
    // Read Directory Entries
    if (filename.size())
    {
        size_t index = 1;
        mstr::replaceAll(filename, "\\", "/");
        bool wildcard = (mstr::contains(filename, "*") || mstr::contains(filename, "?"));
        while (seekEntry(index))
        {
            std::string entryFilename = entry.filename;
            uint8_t i = entryFilename.find_first_of(0xA0);
            entryFilename = entryFilename.substr(0, i);
            //mstr::rtrimA0(entryFilename);
            entryFilename = mstr::toUTF8(entryFilename);

            //Debug_printv("filename[%s] entry.filename[%s]", filename.c_str(), entryFilename.c_str());

            if (filename == entryFilename) // Match exact
            {
                return true;
            }
            else if (wildcard) // Wildcard Match
            {
                if (filename == "*") // Match first PRG
                {
                    filename = entryFilename;
                    return true;
                }
                else if (mstr::compare(filename, entryFilename)) // X?XX?X* Wildcard match
                {
                    return true;
                }
            }

            index++;
        }
    }

    entry.filename[0] = '\0';

    return false;
}


bool ARKMStream::seekEntry( uint16_t index )
{
    Debug_printv("entry_count[%d] entry_index[%d] index[%d]", entry_count, entry_index, index);

    if ( !index || index > entry_count )
        return false;

    // Calculate Entry offset
    // 29 bytes Per Entry + 1 byte to include entry count
    index--;
    uint16_t entryOffset = (index * 29) + 1;

    if (!containerStream->seek(entryOffset))
        return false;

    readContainer((uint8_t *)&entry, sizeof(entry));

    entry_index = index + 1;

    Debug_printv("entry_index[%d] entryOffset[%u] blocks[%u] filename[%s]", entry_index, entryOffset, entry.blocks, entry.filename);

    return true;
}


uint32_t ARKMStream::readFile(uint8_t *buf, uint32_t size)
{
    uint32_t bytesRead = 0;
    bytesRead += containerStream->read(buf, size);

    return bytesRead;
}

bool ARKMStream::seekPath(std::string path)
{
    // Implement this to skip a queue of file streams to start of file by name
    // this will cause the next read to return bytes of 'path'
    seekCalled = true;

    entry_index = 0;

    // call image method to obtain file bytes here, return true on success:
    if (seekEntry(path))
    {
        // auto entry = containerImage->entry;
        auto type = decodeType(entry.file_type);
        media_header_size = (entry_count * 29 + 1);
        media_data_offset = ((media_header_size + (254 - 1)) / 254) * 254; // Round to nearest block
        uint32_t entry_data_offset = media_data_offset;

        // Calculate file size
        uint16_t blocks = entry.blocks;
        _size = ((blocks - 1) * 254) + entry.lsu_byte - 1;

        Debug_printv("entry_index[%d] entry_data_offset[%lu] blocks[%u] _size[%lu] lsu[%d]", entry_index, entry_data_offset, blocks, _size, entry.lsu_byte);

        // Get size of files up to this one to calculate data_offset
        uint8_t c = 1;
        uint8_t t = entry_index;
        while ( c < t )
        {
            seekEntry(c);
            entry_data_offset += ( entry.blocks * 254);
            Debug_printv("c[%d] blocks[%u] entry_index[%d] entry_data_offset[%lu]", c, entry.blocks, entry_index, entry_data_offset);
            c++;
        }
        

        Debug_printv("filename [%.16s] type[%s] data_offset[%lu] blocks[%u] file_size[%lu]", entry.filename, type.c_str(), entry_data_offset, blocks, _size);


        // Set position to beginning of file
        _position = 0;
        containerStream->seek(entry_data_offset);

        Debug_printv("File Size: size[%ld] available[%ld] position[%ld]", _size, available(), _position);

        return true;
    }
    else
    {
        Debug_printv("Not found! [%s]", path.c_str());
    }

    return false;
};

/********************************************************
 * File implementations
 ********************************************************/

bool ARKMFile::isDirectory()
{
    // Debug_printv("pathInStream[%s]", pathInStream.c_str());
    if (pathInStream == "")
        return true;
    else
        return false;
};

bool ARKMFile::rewindDirectory()
{
    dirIsOpen = true;
    Debug_printv("streamFile->url[%s]", streamFile->url.c_str());
    auto image = ImageBroker::obtain<ARKMStream>(streamFile->url);
    if (image == nullptr)
        Debug_printv("image pointer is null");

    image->resetEntryCounter();

    // Read Header
    image->seekHeader();

    // Set Media Info Fields
    media_header = mstr::format("%.16s", image->header.disk_name.c_str());
    media_id = image->header.id_dos;
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    // mstr::toUTF8(media_image);

    Debug_printv("media_header[%s] media_id[%s] media_blocks_free[%d] media_block_size[%d] media_image[%s]", media_header.c_str(), media_id.c_str(), media_blocks_free, media_block_size, media_image.c_str());

    return true;
}

MFile *ARKMFile::getNextFileInDir()
{

    if (!dirIsOpen)
        rewindDirectory();

    // Get entry pointed to by containerStream
    auto image = ImageBroker::obtain<ARKMStream>(streamFile->url);

    if (image->seekNextImageEntry())
    {
        std::string filename = image->entry.filename;
        uint8_t i = filename.find_first_of(0xA0);
        filename = filename.substr(0, i);
        // mstr::rtrimA0(filename);
        mstr::replaceAll(filename, "/", "\\");
        //Debug_printv( "entry[%s]", (streamFile->url + "/" + fileName).c_str() );
        auto file = MFSOwner::File(streamFile->url + "/" + filename);
        file->extension = image->decodeType(image->entry.file_type);
        //Debug_printv("entry[%s] ext[%s]", fileName.c_str(), file->extension.c_str());
        return file;
    }
    else
    {
        // Debug_printv( "END OF DIRECTORY");
        dirIsOpen = false;
        return nullptr;
    }
}

uint32_t ARKMFile::size()
{
    auto entry = ImageBroker::obtain<ARKMStream>(streamFile->url)->entry;
    return UINT16_FROM_LE_UINT16(entry.blocks);
}
