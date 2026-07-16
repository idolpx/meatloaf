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

bool TAPMStream::ensureDecoder()
{
    if (!decoder_tried)
    {
        decoder_tried = true;
        decoder_ok = decoder.open(containerStream.get());
    }
    return decoder_ok;
}

void TAPMStream::setDefaultName(std::string name)
{
    // Media file name without its extension, PETSCII-encoded so it lists
    // like the tape's own (PETSCII) entry names
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
        name = name.substr(0, dot);
    if (!name.empty())
        default_name = mstr::toPETSCII2(name);
}

std::string TAPMStream::entryDisplayName(const TapeEntry &e)
{
    return e.name.empty() ? default_name : e.name;
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

        // "<offset>[:<length>] <name>" - numbers in decimal, 0x-hex or
        // 0-octal; the length field (file size in bytes) is optional
        char *endp = nullptr;
        unsigned long off = strtoul(line.c_str(), &endp, 0);
        if (endp == line.c_str())
            continue;
        unsigned long size = 0;
        if (*endp == ':')
        {
            char *endp2 = nullptr;
            size = strtoul(endp + 1, &endp2, 0);
            if (endp2 != endp + 1)
                endp = endp2;
        }
        std::string name(endp);
        size_t n = name.find_first_not_of(" \t");
        name = (n == std::string::npos) ? "" : name.substr(n);
        if (name.empty())
            name = "entry " + std::to_string(idx_entries.size() + 1);

        // .idx files hold ASCII/UTF8 text; entry names are PETSCII internally
        name = mstr::toPETSCII2(name);

        idx_entries.push_back({ (uint32_t)off, (uint32_t)size, name });
    }

    has_idx = !idx_entries.empty();
    if (has_idx)
        Debug_printv("Using .idx directory: %d entries", idx_entries.size());
}

bool TAPMStream::idxEntry(uint16_t index, std::string &name, uint32_t &size)
{
    if (index >= idx_entries.size())
        return false;
    name = idx_entries[index].name;
    size = idx_entries[index].size;
    return true;
}

bool TAPMStream::setCounterMs(uint32_t ms)
{
    if (!ensureDecoder())
        return false;

    decoder.resetContinuation();
    tape_pos = decoder.offsetAtTime(ms);
    tape_ended = (tape_pos >= decoder.imageLen());
    have_current = false;

    Debug_printv("Tape counter set to %lu ms -> offset[%lu] ended[%d]", ms, tape_pos, tape_ended);
    return true;
}

bool TAPMStream::setCounter(std::string spec)
{
    mstr::trim(spec);
    if (spec.empty())
        return false;

    uint32_t ms;
    size_t colon = spec.find(':');
    if (colon != std::string::npos)
    {
        // "MMM:SS"
        uint32_t mins = strtoul(spec.substr(0, colon).c_str(), nullptr, 10);
        uint32_t secs = strtoul(spec.substr(colon + 1).c_str(), nullptr, 10);
        ms = (mins * 60 + secs) * 1000;
    }
    else
    {
        // milliseconds
        char *endp = nullptr;
        ms = strtoul(spec.c_str(), &endp, 10);
        if (endp == spec.c_str())
            return false;
    }

    return setCounterMs(ms);
}

void TAPMStream::resetTape()
{
    tape_pos = 0;
    tape_ended = false;
    have_current = false;
    decoder.resetContinuation();
}

bool TAPMStream::nextTapeEntry()
{
    if (!ensureDecoder())
    {
        tape_ended = true;
        return false;
    }

    TapeEntry e;
    if (decoder.nextProgram(tape_pos, e))
    {
        tape_pos = e.tape_end_offset;
        current = std::move(e);
        have_current = true;
        return true;
    }

    tape_ended = true;
    have_current = false;
    return false;
}

void TAPMStream::serveCurrent()
{
    _size = current.prg.size();
    _position = 0;
}

bool TAPMStream::seekPath(std::string path)
{
    seekCalled = true;
    _position = 0;
    _size = 0;

    if (mode == std::ios_base::out)
    {
        Debug_printv("Writing to tape images is not supported");
        return false;
    }

    mstr::replaceAll(path, "\\", "/");
    bool wildcard = (mstr::contains(path, "*") || mstr::contains(path, "?"));

    if (has_idx)
    {
        // Random access via the index: decode the program at the offset
        for (auto &ie : idx_entries)
        {
            std::string entryName = mstr::toUTF8(ie.name);
            if (mstr::compareFilename(entryName, path, wildcard))
            {
                if (!ensureDecoder() || !decoder.nextProgram(ie.offset, current))
                {
                    Debug_printv("Failed to decode program at offset %lu", ie.offset);
                    return false;
                }
                have_current = true;
                serveCurrent();
                Debug_printv("Loaded [%s] via idx offset[%lu] loader[%s] size[%u]",
                             ie.name.c_str(), ie.offset, current.loader.c_str(), (unsigned)current.prg.size());
                return true;
            }
        }
        Debug_printv("Not found in idx: [%s]", path.c_str());
        return false;
    }

    // Sequential: the file found by the last directory request is ready
    if (have_current)
    {
        std::string entryName = mstr::toUTF8(entryDisplayName(current));
        if (mstr::compareFilename(entryName, path, wildcard))
        {
            serveCurrent();
            Debug_printv("Loaded [%s] loader[%s] size[%u] (current)",
                         entryName.c_str(), current.loader.c_str(), (unsigned)current.prg.size());
            return true;
        }
    }

    // Otherwise search forward from the current tape position, wrapping
    // around to the beginning once
    bool wrapped = false;
    if (tape_ended)
    {
        resetTape();
        wrapped = true;
    }
    uint32_t scan_start = tape_pos;

    while (true)
    {
        if (!nextTapeEntry())
        {
            if (wrapped || scan_start == 0)
                break; // searched the whole tape
            resetTape();
            wrapped = true;
            continue;
        }

        std::string entryName = mstr::toUTF8(entryDisplayName(current));
        if (mstr::compareFilename(entryName, path, wildcard))
        {
            serveCurrent();
            Debug_printv("Loaded [%s] loader[%s] size[%u]",
                         entryName.c_str(), current.loader.c_str(), (unsigned)current.prg.size());
            return true;
        }

        if (wrapped && current.tape_offset >= scan_start)
            break; // wrapped past where the search began
    }

    Debug_printv("Not found: [%s]", path.c_str());
    return false;
}

uint32_t TAPMStream::readFile(uint8_t *buf, uint32_t size)
{
    if (!have_current || _position >= current.prg.size())
        return 0;
    uint32_t avail = current.prg.size() - _position;
    if (size > avail)
        size = avail;
    memcpy(buf, current.prg.data() + _position, size);
    return size;
}

/********************************************************
 * Tape counter
 ********************************************************/

uint32_t TAPMStream::counterMs()
{
    // Position in time from the beginning of the tape: the current file's
    // start plus read progress scaled across its duration on tape
    if (!have_current)
        return 0;
    if (current.prg.empty() || current.end_time_ms <= current.start_time_ms)
        return current.start_time_ms;

    uint64_t span = current.end_time_ms - current.start_time_ms;
    return current.start_time_ms + (uint32_t)((span * _position) / current.prg.size());
}

uint32_t TAPMStream::durationMs()
{
    if (!ensureDecoder())
        return 0;
    return decoder.totalMs();
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
    // The duration is only known once the tape end has been reached
    uint32_t total = durationMs();
    if (total == 0)
        return format_tape_time(counterMs());
    return format_tape_time(counterMs()) + "/" + format_tape_time(total);
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

/********************************************************
 * File implementations
 ********************************************************/

std::shared_ptr<MStream> TAPMFile::getDecodedStream(std::shared_ptr<MStream> is)
{
    //Debug_printv("[%s]", url.c_str());
    auto stream = std::make_shared<TAPMStream>(is);
    stream->setDefaultName(name);
    stream->loadIndex(readIdxSibling());
    return stream;
}

std::string TAPMFile::idxSiblingPath()
{
    // Same base name as the image, ".idx" extension
    std::string idxPath = url;
    size_t dot = idxPath.find_last_of('.');
    size_t slash = idxPath.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    return idxPath.substr(0, dot) + ".idx";
}

std::string TAPMFile::readIdxSibling()
{
    std::string idxPath = idxSiblingPath();
    if (idxPath.empty())
        return "";

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

bool TAPMFile::buildIndex()
{
    auto image = ImageBroker::obtain<TAPMStream>("tap", url);
    if (image == nullptr)
        return false;

    std::string idxPath = idxSiblingPath();
    if (idxPath.empty())
        return false;

    // Scan the whole tape sequentially, collecting every program
    Debug_printv("Building index for [%s]", url.c_str());
    image->resetTape();

    std::string text = "# TAP index generated by Meatloaf\n";
    text += "# <offset>:<length> <name>\n";
    uint16_t count = 0;

    while (image->nextTapeEntry())
    {
        const TapeEntry &e = image->current;
        char nums[32];
        snprintf(nums, sizeof(nums), "%lu:%lu ",
                 (unsigned long)e.tape_offset, (unsigned long)e.prg.size());
        text += nums;
        text += image->entryDisplayName(e);
        text += "\n";
        count++;
    }

    // Rewind so the next directory request starts from the beginning
    image->resetTape();

    if (count == 0)
    {
        Debug_printv("No programs found; not writing [%s]", idxPath.c_str());
        return false;
    }

    // Write the sidecar via the underlying filesystem
    std::unique_ptr<MFile> f(MFSOwner::File(idxPath));
    if (f == nullptr)
        return false;
    std::shared_ptr<MStream> s = (f->sourceFile != nullptr)
        ? f->sourceFile->getSourceStream(std::ios_base::out)
        : f->getSourceStream(std::ios_base::out);
    if (s == nullptr || !s->isOpen())
    {
        Debug_printv("Cannot write [%s]", idxPath.c_str());
        return false;
    }
    uint32_t written = s->write((const uint8_t *)text.c_str(), text.size());
    s->close();

    if (written != text.size())
    {
        Debug_printv("Short write to [%s] (%lu of %u bytes)", idxPath.c_str(), written, (unsigned)text.size());
        return false;
    }

    Debug_printv("Wrote [%s]: %u entries", idxPath.c_str(), count);

    // Switch the stream to index mode immediately
    image->loadIndex(text);
    return true;
}

bool TAPMFile::rewindDirectory()
{
    dirIsOpen = true;
    entry_index = 0;

    auto image = ImageBroker::obtain<TAPMStream>("tap", url);
    if (image == nullptr)
        return false;

    // Set Media Info Fields (the tape position is NOT reset - listings
    // advance through the tape sequentially)
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

    // .idx present: a normal full directory listing (random access)
    if (image->hasIndex())
    {
        std::string filename;
        uint32_t size = 0;
        if (!image->idxEntry(entry_index, filename, size))
        {
            dirIsOpen = false;
            return nullptr;
        }
        entry_index++;

        mstr::replaceAll(filename, "/", "\\");
        auto file = MFSOwner::File(url + "/" + filename);
        file->name = filename;
        file->extension = "prg";
        file->size = size; // from the .idx length field (0 if absent)
        file->is_dir = 0;
        return file;
    }

    // Sequential tape: each directory request returns ONE entry - the next
    // program on the tape - and leaves it ready to load
    if (entry_index > 0)
    {
        dirIsOpen = false;
        return nullptr;
    }
    entry_index++;

    // The previous request reached the end of the tape: start over
    if (image->tapeEnded())
        image->resetTape();

    if (image->nextTapeEntry())
    {
        std::string filename = image->entryDisplayName(image->current);
        mstr::replaceAll(filename, "/", "\\");

        auto file = MFSOwner::File(url + "/" + filename);
        file->name = filename;
        file->extension = "prg";
        file->size = image->current.prg.size();
        file->is_dir = 0;

        Debug_printv("Tape entry: %s loader[%s] size[%lu] tape[%lu-%lu ms]",
                     filename.c_str(), image->current.loader.c_str(),
                     (unsigned long)file->size,
                     image->current.start_time_ms, image->current.end_time_ms);
        return file;
    }

    // End of the tape reached
    std::string marker = mstr::toPETSCII2("no more entries");
    auto file = MFSOwner::File(url + "/" + marker);
    file->name = marker;
    file->extension = "";
    file->size = 0;
    file->is_dir = 0;
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
        if (stream->hasIndex())
        {
            bool wildcard = (mstr::contains(pathInStream, "*") || mstr::contains(pathInStream, "?"));
            std::string name;
            uint32_t size;
            for (uint16_t i = 0; stream->idxEntry(i, name, size); i++)
            {
                std::string entryName = mstr::toUTF8(name);
                if (mstr::compareFilename(entryName, pathInStream, wildcard))
                    return true;
            }
            return false;
        }
        // Sequential tapes resolve names when loading (seekPath scans)
        return true;
    }

    return true;
}
