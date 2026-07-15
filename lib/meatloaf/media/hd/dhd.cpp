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

#include "dhd.h"

#include "meat_media.h"
#include <cstring>
#include <memory>

std::map<std::string, DHDImageRegistry::Image> DHDImageRegistry::s_images;
bool DHDImageRegistry::s_probing = false;

const DHDPartition* DHDImageRegistry::Image::byNumber(uint8_t number) const
{
    for (auto &p : parts)
    {
        if (p.number == number)
            return &p;
    }
    return nullptr;
}

const DHDPartition* DHDImageRegistry::Image::byName(std::string utf8name) const
{
    bool wildcard = (mstr::contains(utf8name, "*") || mstr::contains(utf8name, "?"));
    for (auto &p : parts)
    {
        std::string pn = mstr::toUTF8(p.name);
        if (mstr::compareFilename(pn, utf8name, wildcard))
            return &p;
    }
    return nullptr;
}

std::string DHDImageRegistry::containerOf(const std::string &path)
{
    // The container path ends with the ".dhd" component
    for (size_t i = 0; i + 4 <= path.size(); i++)
    {
        if ((path[i] == '.') &&
            (path[i+1] == 'd' || path[i+1] == 'D') &&
            (path[i+2] == 'h' || path[i+2] == 'H') &&
            (path[i+3] == 'd' || path[i+3] == 'D'))
        {
            size_t end = i + 4;
            if (end == path.size() || path[end] == '/')
                return path.substr(0, end);
        }
    }
    return "";
}

DHDImageRegistry::Image* DHDImageRegistry::obtain(const std::string &containerUrl)
{
    if (containerUrl.empty())
        return nullptr;

    auto it = s_images.find(containerUrl);
    if (it != s_images.end() && it->second.valid)
        return &it->second;

    // (Re-)parse: not yet seen, or the image wasn't readable last time
    Image img;
    if (!parse(containerUrl, img))
        return nullptr;

    s_images[containerUrl] = std::move(img);
    return &s_images[containerUrl];
}

bool DHDImageRegistry::parse(const std::string &containerUrl, Image &img)
{
    // CMD HD boot code signature found at track 0, sector 5, offset $F0
    // of the system partition
    static const uint8_t hdmagic[16] = {
        0x43, 0x4D, 0x44, 0x20, 0x48, 0x44, 0x20, 0x20,   // "CMD HD  "
        0x8D, 0x03, 0x88, 0x8E, 0x02, 0x88, 0xEA, 0x60
    };

    // Open the raw image bytes: the probing flag makes DHDMFileSystem
    // decline the path so the underlying filesystem serves it
    s_probing = true;
    std::unique_ptr<MFile> f(MFSOwner::File(containerUrl));
    std::shared_ptr<MStream> s = (f != nullptr) ? f->getSourceStream() : nullptr;
    s_probing = false;

    if (s == nullptr || !s->isOpen())
    {
        Debug_printv("Cannot open DHD image [%s]", containerUrl.c_str());
        return false;
    }

    uint32_t image_size = s->size();
    uint8_t cfg[256];

    // The system partition sits on a 64 KiB boundary
    uint32_t sys_base = 0xFFFFFFFF;
    for (uint32_t base = 0; base + 0x600 <= image_size; base += 65536)
    {
        if (!s->seek(base + 0x500))
            break;
        if (s->read(cfg, sizeof(cfg)) != sizeof(cfg))
            break;
        if (memcmp(&cfg[0xF0], hdmagic, sizeof(hdmagic)) == 0)
        {
            sys_base = base;
            break;
        }
    }

    if (sys_base == 0xFFFFFFFF)
    {
        Debug_printv("No CMD HD system partition found [%s]", containerUrl.c_str());
        return false;
    }

    img.default_part = cfg[0xE2];

    // Partition table on track 1 of the system partition: 32-byte entries,
    // 8 per 256-byte sector, laid out contiguously. Entry 0 is the system
    // partition itself.
    uint8_t buf[32];
    for (uint16_t i = 0; i <= 254; i++)
    {
        uint32_t off = sys_base + 65536 + (uint32_t)i * 32;
        if (off + 32 > image_size)
            break;
        if (!s->seek(off))
            break;
        if (s->read(buf, sizeof(buf)) != sizeof(buf))
            break;

        std::string name = std::string((char *)&buf[5], 16);
        size_t e = name.find((char)0xA0);
        if (e != std::string::npos)
            name.resize(e);

        if (i == 0)
        {
            img.disk_label = mstr::toUTF8(name);
            continue;
        }

        uint8_t type = buf[2];
        if (type < 1 || type > 4)
            continue;

        DHDPartition p;
        p.number = i;
        p.type = type;
        p.name = name;
        // 3-byte big-endian offset/size in 512-byte LBA blocks
        p.start = (((uint32_t)buf[0x15] << 16) | ((uint32_t)buf[0x16] << 8) | buf[0x17]) * 512;
        p.size = (((uint32_t)buf[0x1D] << 16) | ((uint32_t)buf[0x1E] << 8) | buf[0x1F]) * 512;
        img.parts.push_back(p);

        Debug_printv("partition[%d] type[%d] name[%s] start[%lu] size[%lu]",
                     p.number, p.type, p.name.c_str(), p.start, p.size);
    }

    if (img.parts.empty())
    {
        Debug_printv("No usable partitions in [%s]", containerUrl.c_str());
        return false;
    }

    // First use: select the default partition
    img.selected = img.byNumber(img.default_part) ? img.default_part : img.parts[0].number;
    img.valid = true;

    Debug_printv("CMD HD [%s] label[%s] partitions[%d] selected[%d]",
                 containerUrl.c_str(), img.disk_label.c_str(), img.parts.size(), img.selected);
    return true;
}

bool DHDImageRegistry::select(const std::string &containerUrl, uint8_t number)
{
    Image* img = obtain(containerUrl);
    if (img == nullptr)
        return false;

    const DHDPartition* p = img->byNumber(number);
    if (p == nullptr)
        return false;

    if (img->selected == number)
        return true;

    img->selected = number;
    Debug_printv("selected partition[%d] type[%d] name[%s]", p->number, p->type, p->name.c_str());

    // Drop the broker-cached image stream so the next access decodes the
    // newly selected partition (key format mirrors ImageBroker::obtain)
    std::unique_ptr<MFile> f(MFSOwner::File(containerUrl));
    if (f != nullptr && f->sourceFile != nullptr)
    {
        std::string key = "d64" + f->sourceFile->url;
        if (f->sourceFile->pathInStream.size() && f->sourceFile->pathInStream != "/")
            key += "/" + f->sourceFile->pathInStream;
        ImageBroker::dispose(key);
    }

    return true;
}
