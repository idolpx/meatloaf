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

#ifndef DRIVE_RAM_H
#define DRIVE_RAM_H

#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <memory>
#include <esp_rom_crc.h>
#include <esp_heap_caps.h>

#include "../../meatloaf/meatloaf.h"
#include "utils.h"

class driveMemory {
private:
    std::vector<uint8_t> ram;  // 0000-07FF  RAM (lazy-allocated on first write)
    size_t _ramSize;           // intended RAM size

    // ROM bytes are shared across all drives that load the same file.
    // Stored in PSRAM when available; freed when the last drive releases it.
    struct RomBytes {
        uint8_t* data = nullptr;
        size_t size = 0;
        ~RomBytes() {
            if (data) {
                free(data);
                data = nullptr;
            }
        }
    };
    std::shared_ptr<RomBytes> rom;

    static std::unordered_map<std::string, std::weak_ptr<RomBytes>>&
    getRomCache() {
        static std::unordered_map<std::string, std::weak_ptr<RomBytes>> cache;
        return cache;
    }

public:
    driveMemory(size_t ramSize = 2048) : _ramSize(ramSize) {}
    ~driveMemory() = default;

    uint16_t mw_hash = 0xFFFF;

    bool setRAM(size_t ramSize) {
        _ramSize = ramSize;
        if (!ram.empty()) ram.resize(ramSize, 0x00);
        return true;
    }

    bool setROM(std::string filename) {
        // Return immediately if a live cached copy already exists
        auto& cache = getRomCache();
        auto it = cache.find(filename);
        if (it != cache.end()) {
            auto locked = it->second.lock();
            if (locked) {
                rom = locked;
                printf("Drive ROM shared: %s (%zu bytes)\r\n", filename.c_str(), rom->size);
                return true;
            }
        }

        // Load from SD/.rom/ then flash /.rom/
        std::unique_ptr<MFile> rom_file(
            MFSOwner::File("/sd/.rom/" + filename, true));
        if (!rom_file)
            rom_file.reset(MFSOwner::File("/.rom/" + filename, true));
        if (!rom_file) return false;

        auto stream = rom_file->getSourceStream();
        if (!stream) return false;

        size_t size = stream->size();

        // Prefer PSRAM to keep ROM bytes out of scarce internal RAM
        uint8_t* buf = nullptr;
#ifdef CONFIG_SPIRAM
        buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        if (!buf) buf = (uint8_t*)malloc(size);
        if (!buf) return false;

        stream->read(buf, size);

        auto romData = std::make_shared<RomBytes>();
        romData->data = buf;
        romData->size = size;
        cache[filename] =
            romData;  // weak ref so cache entry auto-clears when unused
        rom = romData;

        printf("Drive ROM Loaded file[%s] size[%zu]\r\n", rom_file->url.c_str(), rom->size);
        return true;
    }

    size_t read(uint16_t addr, uint8_t* data, size_t len) {
        // RAM
        if (addr < 0x0FFF) {
            if (addr >= 0x0800) addr -= 0x0800;  // RAM Mirror

            if (ram.empty() || addr + len > ram.size()) {
                return 0;
            }

            memcpy(data, &ram[addr], len);
            Debug_printv("RAM read %04X:%s", addr, mstr::toHex(data, len).c_str());
            printf("%s", util_hexdump((const uint8_t*)ram.data(), ram.size()).c_str());
            return len;
        }

        // ROM
        if (addr >= 0x8000) {
            if (addr >= 0xC000)
                addr -= 0xC000;
            else if (addr >= 0x8000)
                addr -= 0x8000;  // ROM Mirror

            if (rom && rom->data) {
                if (addr + len > rom->size) len = rom->size - addr;
                memcpy(data, rom->data + addr, len);
                return len;
            }
            return 0;
        }

        return 0;
    }

    void write(uint16_t addr, const uint8_t* data, size_t len) {
        // RAM
        if (addr < 0x0FFF) {
            if (addr >= 0x0800) addr -= 0x0800;  // RAM Mirror

            // Lazy-allocate RAM on first write
            if (ram.empty()) ram.resize(_ramSize, 0x00);

            if (addr + len > ram.size()) return;
            memcpy(&ram[addr], data, len);
            mw_hash = esp_rom_crc16_be(mw_hash, data, len);
            Debug_printv("RAM write %04X:%s [%d] crc[%04X]", addr, mstr::toHex(data, len).c_str(), len, mw_hash);
        }
    }

    void execute(uint16_t addr) {
        // RAM
        if (addr < 0x0FFF) {
            if (addr >= 0x0800) addr -= 0x0800;  // RAM Mirror

            if (!ram.empty()) {
                Debug_printv("RAM execute %04X", addr);
                printf("%s", util_hexdump((const uint8_t*)ram.data(), ram.size()).c_str());
                mw_hash = 0xFFFF;
            }
        }

        // ROM
        if (rom) {
            if (addr >= 0x8000) {
                if (addr >= 0xC000)
                    addr -= 0xC000;
                else if (addr >= 0x8000)
                    addr -= 0x8000;  // ROM Mirror

                // Translate ROM functions to virtual drive functions
                Debug_printv("ROM execute %04X", addr);
            }
        }
    }

    void reset() {
        if (!ram.empty()) ram.assign(ram.size(), 0x00);
        mw_hash = 0xFFFF;
    }
};

#endif