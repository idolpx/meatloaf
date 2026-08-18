#ifndef FN_FSFTP_H
#define FN_FSFTP_H

#include <cstddef>
#include <memory>
#include <stdint.h>

#include "peoples_url_parser.h"
#include "fnFTP.h"
#include "fnFS.h"
#ifndef DISABLE_DIRCACHE
#include "fnDirCache.h"
#endif


class FileSystemFTP : public FileSystem
{
private:
    // parsed FTP URL
    std::unique_ptr<PeoplesUrlParser> _url;

    // FTP client
    fnFTP *_ftp;

    // stored credentials for reconnection
    std::string _username;
    std::string _password;

    // Path of the listing that is open, or was last opened.
    char _last_dir[MAX_PATHLEN];

#ifndef DISABLE_DIRCACHE
    // Default: the whole listing is read up front and cached, which is what
    // gives sorting, a re-usable listing and O(1) dir_seek().
    DirCache _dircache;
#else
    // DISABLE_DIRCACHE: entries are streamed off the FTP data connection one at
    // a time. A DirCache holds 272 bytes per entry, which on a board without
    // PSRAM comes out of the internal heap and is the largest single cost of an
    // `ls` over FTP; only one entry is held at a time here, so a listing costs
    // the same whether it has 10 entries or 10000. The price is no sorting, no
    // listing re-use, and a dir_seek() that re-lists.
    fsdir_entry _direntry;         // the entry dir_read() hands back
    std::string _dirpattern;       // filter for the open listing
    uint16_t _dirpos = 0;          // entries served so far
    bool _dir_open = false;
#endif

public:
    FileSystemFTP();
    ~FileSystemFTP();

    bool start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_FTP; };
    const char *typestring() override { return type_to_string(FSTYPE_FTP); };

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

    bool exists(const char *path) override;

    bool remove(const char *path) override;

    bool rename(const char *pathFrom, const char *pathTo) override;

    bool is_dir(const char *path) override;
    bool mkdir(const char* path) override;
    bool rmdir(const char* path) override;
    bool dir_exists(const char* path) override;

    bool dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t pos) override;

    bool keep_alive() override;

private:
    bool ensure_connected();  // Check connection and reconnect if needed
#ifdef DISABLE_DIRCACHE
    bool dir_start(const char *path);  // (re)issue LIST and reset the cursor
#endif

public:
#ifndef FNIO_IS_STDIO
    FileHandler *cache_file(const char *path, const char *mode);
#endif

};

#endif // FN_FSFTP_H