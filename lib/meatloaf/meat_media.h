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

#ifndef MEATLOAF_MEDIA
#define MEATLOAF_MEDIA

#include "meatloaf.h"

#include <map>
#include <bitset>
#include <unordered_map>
#include <sstream>
#include <chrono>

#include "../../include/debug.h"

#include "../device/iec/meatloaf.h"
#include "../device/iec/fuji.h"
#include "string_utils.h"


/********************************************************
 * Streams
 ********************************************************/

class MMediaStream: public MStream {

public:
    MMediaStream(std::shared_ptr<MStream> is): MStream(is->url) {
        containerStream = is;
        _is_open = true;
        has_subdirs = false;
        //Debug_printv("url[%s]", url.c_str());
    }

    ~MMediaStream() {
        //Debug_printv("close");
        close();
    }

    void reset() override {
        seekCalled = false;
        _position = 0;
        _size = block_size;
        //m_load_address = {0, 0};
    }

    // MStream methods
    bool isOpen() override;

    // Browsable streams might call seekNextEntry to skip current bytes
    bool isBrowsable() override { return false; };
    // Random access streams might call seekPath to jump to a specific file
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    // read = (size) => this.containerStream.read(size);
    virtual uint8_t read();
    // readUntil = (delimiter = 0x00) => this.containerStream.readUntil(delimiter);
    virtual std::string readUntil( uint8_t delimiter = 0x00 );
    // readString = (size) => this.containerStream.readString(size);
    virtual std::string readString( uint8_t size );
    // readStringUntil = (delimiter = 0x00) => this.containerStream.readStringUntil(delimiter);
    virtual std::string readStringUntil( uint8_t delimiter = '\0' );

    virtual uint32_t write(const uint8_t *buf, uint32_t size);

    // seek = (offset) => this.containerStream.seek(offset + this.media_header_size);
    bool seek(uint32_t offset) override;
    // seekCurrent = (offset) => this.containerStream.seekCurrent(offset);
    bool seekCurrent(uint32_t offset);

    bool seekPath(std::string path) override { return false; };
    std::string seekNextEntry() override { return ""; };

    virtual uint32_t seekFileSize( uint8_t start_track, uint8_t start_sector );


protected:

    bool seekCalled = false;
    std::shared_ptr<MStream> containerStream;

    bool _is_open = false;

    MMediaStream* decodedStream;

    bool show_hidden = false;

    size_t media_header_size = 0x00;
    size_t media_data_offset = 0x00;
    size_t entry_index = 0;         // Currently selected directory entry (0 no selection)
    size_t entry_count = -1;        // Directory list entry count (-1 unknown)
    size_t partition_index = 0;     // Currently selected partition (0 no selection)
    size_t partition_count = -1;    // Partition count (-1 unknown)

    enum open_modes { OPEN_READ, OPEN_WRITE, OPEN_APPEND, OPEN_MODIFY };

    // Partition methods
    std::string partition_type_label[9] = { "", "NAT", "41", "71", "81", "C81", "PRN", "FOR", "SYS" };

    // /* These are address/value pairs used by some programs to detect a 1541. */
    // /* Currently we remember two bytes per address since that's the longest  */
    // /* block required. */
    // static const PROGMEM magic_value_t c1541_magics[] = {
    // { 0xfea0, { 0x0d, 0xed } }, /* used by DreamLoad and ULoad Model 3 */
    // { 0xe5c6, { 0x34, 0xb1 } }, /* used by DreamLoad and ULoad Model 3 */
    // { 0xfffe, { 0x00, 0x00 } }, /* Disable AR6 fastloader */
    // { 0,      { 0, 0 } }        /* end mark */
    // };

    // /* System partition G-P answer */
    // static const PROGMEM uint8_t system_partition_info[] = {
    // 0xff,0xe2,0x00,0x53,0x59,0x53,0x54,0x45,
    // 0x4d,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,
    // 0xa0,0xa0,0xa0,0x00,0x00,0x00,0x00,0x00,
    // 0x00,0x00,0x00,0x00,0x00,0x00,0x0d
    // };

    virtual bool seekPartition( uint8_t index ) { return false; };
    virtual bool readPartition( uint8_t index ) { return false; };
    virtual bool writePartition( uint8_t index ) { return false; };

    void resetPartitionCounter() {
        partition_index = 0;
    }
    virtual bool getNextPartitionEntry()
    {
        return seekPartition(partition_index + 1);
    }

    // Directory methods
    std::string file_type_label[12] = { "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "DIR", "???", "SYS", "NAT", "CMD", "CFS" };

    virtual bool readHeader() { return true; }
    virtual bool writeHeader(std::string name, std::string id) { return false; };

    virtual bool seekEntry( std::string filename ) { return false; };
    virtual bool seekEntry( uint16_t index ) { return false; };
    virtual bool readEntry( uint16_t index ) { return false; };
    virtual bool writeEntry( uint16_t index ) { return false; };

    void resetEntryCounter() {
        entry_index = 0;
    }
    virtual bool getNextImageEntry() {
        return seekEntry(entry_index + 1);
    }

    // Disk methods
    virtual uint16_t blocksFree() { return 0; };
	virtual uint8_t speedZone(uint8_t track) { return 0; };

    virtual uint32_t blocks() {
        if ( _size > 0 && _size < block_size )
            return 1;
        else
            return ( _size / block_size );
    }

    virtual uint32_t readContainer(uint8_t *buf, uint32_t size);
    virtual uint32_t writeContainer(uint8_t *buf, uint32_t size);
    virtual uint32_t readFile(uint8_t* buf, uint32_t size) = 0;
    virtual uint32_t writeFile(uint8_t* buf, uint32_t size) = 0;

    virtual bool isDirectory(uint8_t file_type);
    virtual std::string decodeType(uint8_t file_type, bool show_hidden = false);
    virtual std::string decodeType(std::string file_type);
    virtual std::string decodeGEOSType(uint8_t geos_file_structure, uint8_t geos_file_type);

private:

    // Commodore Media
    // CARTRIDGE
    friend class CRTFile;

    // CONTAINER
    friend class D8BMFile;
    friend class DFIMFile;

    // FLOPPY DISK
    friend class D64MFile;
    friend class D71MFile;
    friend class D80MFile;
    friend class D81MFile;
    friend class D82MFile;

    // HARD DRIVE
    friend class DNPMFile;
    friend class D90MFile;

    // FILE
    friend class PRGMFile;
    friend class P00MFile;

    // CASSETTE TAPE
    friend class T64MFile;
    friend class TCRTMFile;
};



/********************************************************
 * Utility implementations
 ********************************************************/
class ImageBroker {
    // LRU entry: stores key + last access timestamp for eviction
    struct LRUEntry {
        std::string key;
        std::chrono::steady_clock::time_point last_access;
        LRUEntry(std::string k) : key(std::move(k)), last_access(std::chrono::steady_clock::now()) {}
    };

    static std::unordered_map<std::string, std::shared_ptr<MMediaStream>> image_repo;
    static std::vector<LRUEntry> lru_order;  // oldest-first LRU list
    static std::chrono::steady_clock::time_point last_cleanup;

    static constexpr size_t max_entries = 50;              // Max cached streams
    static constexpr unsigned int cleanup_interval_ms = 60000; // Cleanup every 60s

    // Check if an entry is currently in use by any active drive
    static bool is_in_use(const std::string& key) {
        for (int i = 0; i < MAX_DISK_DEVICES; i++) {
            auto drive = Meatloaf.get_disks(i);
            if (drive != nullptr) {
                auto cwd = drive->disk_dev.getCWD();
                if (cwd.back() == '/') cwd.pop_back();
                if (!cwd.empty() && mstr::endsWith(key, cwd.c_str())) {
                    return true;
                }
            }
        }
        return false;
    }

    // Evict oldest entries if over limit (skip entries that are in use)
    static void evict_lru_if_needed() {
        while (lru_order.size() >= max_entries) {
            // Find oldest entry that is NOT in use
            auto it = lru_order.begin();
            for (; it != lru_order.end(); ++it) {
                if (!is_in_use(it->key)) {
                    Debug_printv("LRU evicting: %s", it->key.c_str());
                    image_repo.erase(it->key);
                    lru_order.erase(it);
                    break;
                }
            }
            // If all entries are in use, cannot evict - exit loop
            if (it == lru_order.end()) {
                Debug_printv("LRU: all entries in use, cannot evict");
                break;
            }
        }
    }

    // Periodic cleanup: remove entries with stale timestamps (skip entries that are in use)
    static void cleanup_old_entries() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cleanup).count();
        if (elapsed < cleanup_interval_ms) return;


        last_cleanup = now;
        auto it = lru_order.begin();
        while (it != lru_order.end()) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->last_access).count();
            if (age >= cleanup_interval_ms && !is_in_use(it->key)) {
                Debug_printv("LRU stale evicting: %s", it->key.c_str());
                image_repo.erase(it->key);
                it = lru_order.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Move existing key to back (most-recently-used)
    static void touch_entry(const std::string& key) {
        for (auto& e : lru_order) {
            if (e.key == key) {
                e.last_access = std::chrono::steady_clock::now();
                return;
            }
        }
    }

public:
    template<class T> static std::shared_ptr<T> obtain(std::string type, std::string url)
    {
        auto newFile = std::unique_ptr<MFile>(MFSOwner::File(url));

        std::string key = type + newFile->sourceFile->url;
        if ( newFile->sourceFile->pathInStream.size() && newFile->sourceFile->pathInStream != "/" )
            key += "/" + newFile->sourceFile->pathInStream;

        auto it = image_repo.find(key);
        if (it != image_repo.end()) {
            touch_entry(key);
            return std::static_pointer_cast<T>(it->second);
        }

        cleanup_old_entries();
        evict_lru_if_needed();

        std::shared_ptr<T> newStream = std::static_pointer_cast<T>(newFile->getSourceStream());

        if ( newStream != nullptr )
        {
            image_repo.insert(std::make_pair(key, newStream));
            lru_order.emplace_back(key);
            return newStream;
        }

        return nullptr;
    }

    static std::shared_ptr<MMediaStream> obtain(std::string type, std::string url) {
        return obtain<MMediaStream>(type, url);
    }

    static bool exists(std::string url) {
        return image_repo.find(url) != image_repo.end();
    }

    static void dispose(std::string url) {
        auto it = image_repo.find(url);
        if (it != image_repo.end()) {
            lru_order.erase(
                std::remove_if(lru_order.begin(), lru_order.end(),
                    [&url](const LRUEntry& e) { return e.key == url; }),
                lru_order.end()
            );
            image_repo.erase(it);
        }
    }

    static void validate() {
        for (auto& pair : image_repo) {
            bool found = false;
            for (int i = 0; i < MAX_DISK_DEVICES; i++) {
                auto drive = Meatloaf.get_disks(i);
                if (drive != nullptr) {
                    auto cwd = drive->disk_dev.getCWD();
                    if (cwd.back() == '/') cwd.pop_back();
                    if (cwd.empty()) continue;
                    if (mstr::endsWith(pair.first, cwd.c_str())) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                Debug_printv("DISPOSING key[%s] stream[%s]", pair.first.c_str(), pair.second->url.c_str());
                dispose(pair.first);
            }
        }
    }

    static void clear() {
        image_repo.clear();
        lru_order.clear();
    }

    static void dump() {
        Debug_printv("streams[%d]", image_repo.size());
        for (auto& pair : image_repo) {
            Debug_printv("key[%s] stream[%s] size[%d]", pair.first.c_str(), pair.second->url.c_str(), sizeof(*pair.second));
        }
    }
};

#endif // MEATLOAF_MEDIA
