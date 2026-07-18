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

#include "m2i.h"

/********************************************************
 * Streams
 ********************************************************/

std::string M2IMStream::decodeType(uint8_t file_type, bool show_hidden)
{
    switch (file_type)
    {
        case 'S': return " SEQ";
        case 'U': return " USR";
        case 'D': return " DEL";
        default:  return " PRG";
    }
}

bool M2IMStream::readHeader()
{
    // Parse the whole index once; M2I files are a few hundred bytes.
    // Nominally fixed-width records, but files in the wild have unpadded
    // CBM names and even a UTF-8 BOM, so parse line by line at the ':'s.
    uint32_t len = containerStream->size();
    if (len < 3 || len > 65536)
        return false;

    std::string text(len, '\0');
    containerStream->seek(0);
    if (readContainer((uint8_t *)&text[0], len) != len)
        return false;

    // UTF-8 BOM before the title (seen in the wild)
    size_t pos = 0;
    if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
        pos = 3;

    title.clear();
    entries.clear();

    bool first = true;
    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (first)
        {
            first = false;
            title = line.substr(0, 16);
            mstr::rtrim(title);
            continue;
        }

        // "T:DOSNAME.EXT :CBMNAME"
        if (line.size() < 4 || line[1] != ':')
            continue;
        char type = line[0];
        // 'D' entries have a blank dosname and separator art as the CBM
        // name (e.g. "----------------"); they are listed as DEL lines.
        // Only '-' free slots are dropped.
        if (type != 'P' && type != 'S' && type != 'U' && type != 'D')
            continue;
        size_t sep = line.find(':', 2);
        if (sep == std::string::npos)
            continue;

        Entry e;
        e.type = type;
        e.dosname = line.substr(2, sep - 2);
        e.cbmname = line.substr(sep + 1);
        mstr::rtrim(e.dosname);
        mstr::rtrim(e.cbmname);
        if (e.cbmname.empty() || (e.dosname.empty() && type != 'D'))
            continue;
        entries.push_back(e);
    }

    Debug_printv("title[%s] entries[%u]", title.c_str(), (unsigned)entries.size());
    return true;
}

bool M2IMStream::seekEntry( uint16_t index )
{
    if (index == 0 || index > entries.size())
        return false;

    entry = entries[index - 1];
    entry_index = index;
    return true;
}

bool M2IMStream::seekEntry( std::string filename )
{
    if (filename.empty())
        return false;

    mstr::replaceAll(filename, "\\", "/");
    bool wildcard = ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );

    uint16_t index = 1;
    while ( seekEntry( index ) )
    {
        // DEL separator lines are listed but never loadable
        if ( entry.type != 'D' )
        {
            std::string entryFilename = mstr::toUTF8(entry.cbmname);

            //Debug_printv("filename[%s] entry[%s]", filename.c_str(), entryFilename.c_str());

            if ( mstr::compareFilename(entryFilename, filename, wildcard) )
                return true;
        }

        index++;
    }

    entry.cbmname.clear();
    return false;
}

// URL of the current entry's host file: sibling of the .m2i
std::string M2IMStream::entryTargetUrl()
{
    std::string dosname = entry.dosname;

    std::string base = containerStream->url;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos)
        base = base.substr(0, slash);

    return base + "/" + dosname;
}

bool M2IMStream::seekPath(std::string path)
{
    seekCalled = true;
    entry_index = 0;
    fileStream = nullptr;
    _position = 0;
    _size = 0;

    if ( !seekEntry(path) )
    {
        Debug_printv("Not found! [%s]", path.c_str());
        return false;
    }

    // Open the entry's host file; the M2I was made for FAT (case
    // insensitive), so fall back to a lowercase name on case-sensitive
    // filesystems
    std::string target = entryTargetUrl();
    std::unique_ptr<MFile> f(MFSOwner::File(target));
    if (f != nullptr)
        fileStream = f->getSourceStream(std::ios_base::in);

    if (fileStream == nullptr || !fileStream->isOpen())
    {
        std::string lower = target;
        size_t slash = lower.find_last_of('/');
        for (size_t i = (slash == std::string::npos ? 0 : slash + 1); i < lower.size(); i++)
            lower[i] = tolower((unsigned char)lower[i]);

        Debug_printv("Retrying lowercase [%s]", lower.c_str());
        std::unique_ptr<MFile> lf(MFSOwner::File(lower));
        if (lf != nullptr)
            fileStream = lf->getSourceStream(std::ios_base::in);
    }

    if (fileStream == nullptr || !fileStream->isOpen())
    {
        Debug_printv("Cannot open target [%s]", target.c_str());
        fileStream = nullptr;
        return false;
    }

    _size = fileStream->size();
    Debug_printv("entry[%s] target[%s] size[%lu]", path.c_str(), target.c_str(), _size);
    return true;
}

uint32_t M2IMStream::readFile(uint8_t* buf, uint32_t size)
{
    if (fileStream == nullptr)
        return 0;
    return fileStream->read(buf, size);
}


/********************************************************
 * File implementations
 ********************************************************/

bool M2IMFile::rewindDirectory()
{
    dirIsOpen = true;
    auto image = ImageBroker::obtain<M2IMStream>("m2i", url);
    if (image == nullptr)
        return false;

    image->resetEntryCounter();

    // Set Media Info Fields
    media_header = image->title;
    media_id = "M2I";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}

MFile* M2IMFile::getNextFileInDir()
{
    if (!dirIsOpen)
        rewindDirectory();

    auto image = ImageBroker::obtain<M2IMStream>("m2i", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return nullptr;
    }

    if ( image->getNextImageEntry() )
    {
        std::string filename = image->entry.cbmname;
        mstr::replaceAll(filename, "/", "\\");

        auto file = MFSOwner::File(url + "/" + filename);
        file->name = filename;
        file->extension = image->decodeType(image->entry.type);
        file->is_dir = 0;

        // Size comes from the host file next to the .m2i (DEL separator
        // lines have no host file)
        if (image->entry.type != 'D')
        {
            std::unique_ptr<MFile> target(MFSOwner::File(image->entryTargetUrl()));
            if (target != nullptr)
            {
                file->size = target->size;
                if (file->size == 0)
                {
                    auto s = target->getSourceStream(std::ios_base::in);
                    if (s != nullptr && s->isOpen())
                        file->size = s->size();
                }
            }
        }

        Debug_printv("entry[%s] ext[%s] size[%lu]", filename.c_str(), file->extension.c_str(), file->size);
        return file;
    }

    dirIsOpen = false;
    return nullptr;
}
