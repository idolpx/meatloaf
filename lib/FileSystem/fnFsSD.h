#ifndef _FN_FSSD_
#define _FN_FSSD_

#ifdef ESP_PLATFORM
#include "esp_vfs_fat.h"
#endif

#include <stdio.h>

#include "fnFS.h"

#ifdef ESP_PLATFORM
// FatFs only carries a per-volume `ssize` when built for VARIABLE sector sizes
// (FF_MAX_SS != FF_MIN_SS). When they are equal the member is compiled out and
// the sector size is that constant, so reading fsinfo->ssize stops compiling.
// Both builds are reachable - FF_MAX_SS is MAX(FF_SS_SDCARD, FF_SS_WL), so it
// depends on CONFIG_WL_SECTOR_SIZE - hence one accessor instead of assuming.
static inline uint32_t fatfs_sector_size(const FATFS* fsinfo)
{
#if FF_MAX_SS != FF_MIN_SS
    return fsinfo->ssize;
#else
    (void)fsinfo;
    return FF_MAX_SS;
#endif
}
#endif

class FileSystemSDFAT : public FileSystem
{
private:
#ifdef ESP_PLATFORM
    FF_DIR _dir;
    sdmmc_card_t *_sdcard_info = nullptr;
#else
    DIR * _dir;
#endif
    uint64_t _card_capacity = 0;
public:
#ifdef ESP_PLATFORM
    bool start();
    // Flushes FATFS's cached FAT/directory sectors and releases the card.
    // Must run before any reset/reboot or the filesystem is left dirty.
    bool stop();
#else
    bool start(const char *sd_path = nullptr);
#endif
    virtual bool is_global() override { return true; };

    fsType type() override { return FSTYPE_SDFAT; };
    const char * typestring() override { return type_to_string(FSTYPE_SDFAT); };

    long filesize(const char *filepath) override;

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool is_dir(const char *path) override;
    bool mkdir(const char* path) override;
    bool rmdir(const char* path) override;
    bool dir_exists(const char* path) override { return true; };

    bool dir_open(const char * path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t) override;

    bool create_path(const char *path);
    
    uint64_t card_size();
    uint64_t total_bytes();
    uint64_t used_bytes();
    const char *partition_type();

#ifdef ESP_PLATFORM
    bool format();
#endif

    // TODO: make it part of base FileSystem class (similar to filesize)
    long mtime(const char *path);
};

extern FileSystemSDFAT fnSDFAT;

#endif // _FN_FSSD_
