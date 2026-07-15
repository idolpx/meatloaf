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

#include "endianness.h"
#include <cstring>
#include <cstdlib>

/********************************************************
 * Streams
 ********************************************************/

bool TAPMStream::loadImage()
{
    if (image_data != nullptr)
        return true;

    uint32_t len = containerStream->size();
    if (len < 20)
    {
        Debug_printv("Tape image too small (%lu bytes)", len);
        return false;
    }

    image_data = (uint8_t *)malloc(len);
    if (image_data == nullptr)
    {
        Debug_printv("Cannot allocate %lu bytes for tape image", len);
        return false;
    }

    containerStream->seek(0);
    uint32_t got = 0;
    while (got < len)
    {
        uint32_t n = containerStream->read(image_data + got, len - got);
        if (n == 0)
            break;
        got += n;
    }

    if (got < len)
    {
        Debug_printv("Short read: %lu of %lu bytes", got, len);
        freeImage();
        return false;
    }

    image_len = len;
    return true;
}

void TAPMStream::freeImage()
{
    if (image_data != nullptr)
    {
        free(image_data);
        image_data = nullptr;
    }
    image_len = 0;
}

void TAPMStream::loadIndex(const std::string &idx_text)
{
    idx_entries.clear();
    has_idx = false;

    size_t pos = 0;
    while (pos < idx_text.size())
    {
        size_t eol = idx_text.find('\n', pos);
        if (eol == std::string::npos)
            eol = idx_text.size();
        std::string line = idx_text.substr(pos, eol - pos);
        pos = eol + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos)
            continue;
        line = line.substr(s);

        // Comments: #, ; or '
        if (line[0] == '#' || line[0] == ';' || line[0] == '\'')
            continue;

        // "<offset> <name>" - offset in decimal, 0x-hex or 0-octal
        char *endp = nullptr;
        unsigned long off = strtoul(line.c_str(), &endp, 0);
        if (endp == line.c_str())
            continue;
        std::string name(endp);
        size_t n = name.find_first_not_of(" \t");
        name = (n == std::string::npos) ? "" : name.substr(n);
        if (name.empty())
            name = "entry " + std::to_string(idx_entries.size() + 1);

        idx_entries.push_back({ (uint32_t)off, name });
    }

    has_idx = !idx_entries.empty();
    if (has_idx)
        Debug_printv("Using .idx directory: %d entries", idx_entries.size());
}

bool TAPMStream::ensureAnalyzed()
{
    if (analyzed)
        return analysis_ok;
    analyzed = true;

    if (!loadImage())
        return false;

    analysis_ok = TapeDecoder::analyze(image_data, image_len, 0, false, entries);
    if (analysis_ok && !times_computed)
    {
        total_ms = TapeDecoder::computeTimes(image_data, image_len, entries);
        times_computed = true;
    }
    return analysis_ok;
}

bool TAPMStream::decodeAt(uint32_t offset, TapeEntry &out)
{
    if (!loadImage())
        return false;

    std::vector<TapeEntry> found;
    if (!TapeDecoder::analyze(image_data, image_len, offset, true, found) || found.empty())
        return false;

    out = std::move(found[0]);
    return true;
}

uint16_t TAPMStream::entryCount()
{
    if (has_idx)
        return idx_entries.size();
    if (!ensureAnalyzed())
        return 0;
    return entries.size();
}

bool TAPMStream::getEntry(uint16_t index, std::string &name, std::string &loader,
                          uint32_t &size, bool &checksum_ok)
{
    if (has_idx)
    {
        if (index >= idx_entries.size())
            return false;
        name = idx_entries[index].name;
        loader = "";
        size = 0;         // unknown without decoding
        checksum_ok = true;
        return true;
    }

    if (!ensureAnalyzed() || index >= entries.size())
        return false;

    name = entries[index].name;
    loader = entries[index].loader;
    size = entries[index].prg.size();
    checksum_ok = entries[index].checksum_ok;
    return true;
}

bool TAPMStream::seekPath(std::string path)
{
    seekCalled = true;
    _position = 0;
    _size = 0;
    current_prg.clear();

    if (mode == std::ios_base::out)
    {
        Debug_printv("Writing to tape images is not supported");
        return false;
    }

    mstr::replaceAll(path, "\\", "/");
    bool wildcard = (mstr::contains(path, "*") || mstr::contains(path, "?"));

    if (has_idx)
    {
        // Find the entry in the index, then decode the program at its offset
        for (auto &ie : idx_entries)
        {
            std::string entryName = mstr::toUTF8(ie.name);
            if (mstr::compareFilename(entryName, path, wildcard))
            {
                TapeEntry e;
                if (!decodeAt(ie.offset, e))
                {
                    Debug_printv("Failed to decode program at offset %lu", ie.offset);
                    return false;
                }
                Debug_printv("Loaded [%s] via idx offset[%lu] loader[%s] size[%u]",
                             ie.name.c_str(), ie.offset, e.loader.c_str(), (unsigned)e.prg.size());
                std::vector<TapeEntry> one;
                one.push_back(TapeEntry());
                one[0].tape_offset = e.tape_offset;
                one[0].tape_end_offset = e.tape_end_offset;
                total_ms = TapeDecoder::computeTimes(image_data, image_len, one);
                times_computed = true;
                cur_start_ms = one[0].start_time_ms;
                cur_end_ms = one[0].end_time_ms;
                current_prg = std::move(e.prg);
                _size = current_prg.size();
                return true;
            }
        }
        Debug_printv("Not found in idx: [%s]", path.c_str());
        return false;
    }

    if (!ensureAnalyzed())
        return false;

    for (auto &e : entries)
    {
        std::string entryName = mstr::toUTF8(e.name);
        if (mstr::compareFilename(entryName, path, wildcard))
        {
            Debug_printv("Loaded [%s] loader[%s] size[%u] tape[%lu-%lu ms]",
                         e.name.c_str(), e.loader.c_str(), (unsigned)e.prg.size(),
                         e.start_time_ms, e.end_time_ms);
            current_prg = e.prg;
            cur_start_ms = e.start_time_ms;
            cur_end_ms = e.end_time_ms;
            _size = current_prg.size();
            return true;
        }
    }

    Debug_printv("Not found: [%s]", path.c_str());
    return false;
}

/********************************************************
 * Tape counter
 ********************************************************/

uint32_t TAPMStream::counterMs()
{
    // Position in time from the beginning of the tape: the selected file's
    // block start plus read progress scaled across the block's duration
    if (current_prg.empty() || cur_end_ms <= cur_start_ms)
        return cur_start_ms;

    uint64_t span = cur_end_ms - cur_start_ms;
    return cur_start_ms + (uint32_t)((span * _position) / current_prg.size());
}

uint32_t TAPMStream::durationMs()
{
    if (!times_computed && loadImage())
    {
        std::vector<TapeEntry> none;
        total_ms = TapeDecoder::computeTimes(image_data, image_len, none);
        times_computed = true;
    }
    return total_ms;
}

static std::string format_tape_time(uint32_t ms)
{
    uint32_t secs = ms / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    return buf;
}

std::string TAPMStream::counterString()
{
    return format_tape_time(counterMs()) + "/" + format_tape_time(durationMs());
}

std::unordered_map<std::string, std::string> TAPMStream::info()
{
    return {
        {"System", "Commodore"},
        {"Format", "TAP"},
        {"Media Type", "TAPE"},
        {"Counter", counterString()},
        {"Duration", format_tape_time(durationMs())}
    };
}

uint32_t TAPMStream::readFile(uint8_t *buf, uint32_t size)
{
    if (_position >= current_prg.size())
        return 0;
    uint32_t avail = current_prg.size() - _position;
    if (size > avail)
        size = avail;
    memcpy(buf, current_prg.data() + _position, size);
    return size;
}

/********************************************************
 * File implementations
 ********************************************************/

std::shared_ptr<MStream> TAPMFile::getDecodedStream(std::shared_ptr<MStream> is)
{
    //Debug_printv("[%s]", url.c_str());
    auto stream = std::make_shared<TAPMStream>(is);
    stream->loadIndex(readIdxSibling());
    return stream;
}

std::string TAPMFile::readIdxSibling()
{
    // Same base name as the image, ".idx" extension
    std::string idxPath = url;
    size_t dot = idxPath.find_last_of('.');
    size_t slash = idxPath.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    idxPath = idxPath.substr(0, dot) + ".idx";

    std::unique_ptr<MFile> f(MFSOwner::File(idxPath));
    if (f == nullptr || !f->exists())
        return "";

    // Read the raw sidecar bytes (via the underlying filesystem)
    std::shared_ptr<MStream> s = (f->sourceFile != nullptr)
        ? f->sourceFile->getSourceStream()
        : f->getSourceStream();
    if (s == nullptr || !s->isOpen())
        return "";

    uint32_t len = s->size();
    if (len == 0 || len > 65536)
        return "";

    std::string text(len, '\0');
    s->seek(0);
    uint32_t got = s->read((uint8_t *)&text[0], len);
    text.resize(got);

    Debug_printv("Found index file [%s] (%lu bytes)", idxPath.c_str(), got);
    return text;
}

bool TAPMFile::rewindDirectory()
{
    dirIsOpen = true;
    entry_index = 0;

    auto image = ImageBroker::obtain<TAPMStream>("tap", url);
    if (image == nullptr)
        return false;

    // Set Media Info Fields
    media_header = image->media_label;
    media_id = "tap";
    media_blocks_free = 0;
    media_block_size = image->block_size;
    media_image = name;
    if ( !sourceFile->media_archive.empty() )
        media_archive = sourceFile->media_archive;

    return true;
}

MFile* TAPMFile::getNextFileInDir()
{
    if (!dirIsOpen)
        rewindDirectory();

    auto image = ImageBroker::obtain<TAPMStream>("tap", url);
    if (image == nullptr)
    {
        dirIsOpen = false;
        return nullptr;
    }

    std::string filename, loader;
    uint32_t size = 0;
    bool checksum_ok = true;

    if (!image->getEntry(entry_index, filename, loader, size, checksum_ok))
    {
        dirIsOpen = false;
        return nullptr;
    }
    entry_index++;

    mstr::replaceAll(filename, "/", "\\");

    auto file = MFSOwner::File(url + "/" + filename);
    file->name = filename;  // Use actual entry name, not container image name
    file->extension = "prg";
    file->size = size;
    file->is_dir = 0;

    //Debug_printv("Entry: %s Loader:%s Size:%lu", filename.c_str(), loader.c_str(), size);

    return file;
}

bool TAPMFile::isDirectory()
{
    if (is_dir != -1)
        return is_dir == 1;

    // The image itself is a directory; entries inside it are files
    return pathInStream.empty() || pathInStream == "/";
}

bool TAPMFile::exists()
{
    auto stream = ImageBroker::obtain<TAPMStream>("tap", url);
    if (stream == nullptr)
        return false;

    if (pathInStream.size() && pathInStream != "/")
    {
        std::string name, loader;
        uint32_t size;
        bool csum;
        bool wildcard = (mstr::contains(pathInStream, "*") || mstr::contains(pathInStream, "?"));
        for (uint16_t i = 0; stream->getEntry(i, name, loader, size, csum); i++)
        {
            std::string entryName = mstr::toUTF8(name);
            if (mstr::compareFilename(entryName, pathInStream, wildcard))
                return true;
        }
        return false;
    }

    return true;
}
