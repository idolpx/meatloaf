
#include "fnFsFTP.h"

#include "compat_string.h"

#include "../../include/debug.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "fnSystem.h"
#include "fnFileCache.h"
#include "utils.h"

#define COPY_BLK_SIZE 4096

FileSystemFTP::FileSystemFTP()
{
    Debug_printf("FileSystemFTP::ctor\n");
    _ftp = nullptr;
    _url = nullptr;
    // invalidate _last_dir
    _last_dir[0] = '\0';
}

FileSystemFTP::~FileSystemFTP()
{
    Debug_printf("FileSystemFTP::dtor\n");
    if (_started)
    {
#ifndef DISABLE_DIRCACHE
        _dircache.clear();
#endif
        _ftp->close_directory();
        _ftp->logout();
    }
    if (_ftp != nullptr)
        delete _ftp;
}

bool FileSystemFTP::start(const char *url, const char *user, const char *password)
{
    bool res;

    if (_started)
        return false;

    if(url == nullptr || url[0] == '\0')
        return false;

    if (_ftp != nullptr)
        delete _ftp;

    _ftp = new fnFTP();
    if (_ftp == nullptr)
    {
        Debug_printf("FileSystemFTP::start() - failed to create FTP client\n");
        return false;
    }

    _url = PeoplesUrlParser::parseURL(url);
    if (!_url->isValidUrl())
    {
        Debug_printf("FileSystemFTP::start() - failed to parse URL \"%s\"\n", url);
        return false;
    }

    // Store credentials for reconnection
    _username = (user == nullptr ? "anonymous" : user);
    _password = (password == nullptr ? "fujinet@fujinet.online" : password);

    res = _ftp->login(
        _username.c_str(),
        _password.c_str(),
        _url->host,
        _url->port.empty() ? 21 : atoi(_url->port.c_str())
    );

	if (res)
    {
        Debug_printf("FileSystemFTP::start() - FTP login failed: %s\n", _url->host.c_str());
        return false;
	}

    Debug_printf("FTP logged in: %s\n", _url->host.c_str());

    _started = true;

    return true;
}

bool FileSystemFTP::exists(const char *path)
{
    if (!ensure_connected() || path == nullptr)
        return false;

    Debug_printf("FileSystemFTP::exists(\"%s\")\n", path);

    // Use LIST to check if path exists (works for both files and directories)
    bool res = _ftp->open_directory(path, "");
    
    if (res != 0)  // open_directory returns 0 on success
    {
        Debug_printf("Path does not exist\n");
        return false;
    }

    // Read at least one entry to confirm it exists
    string filename;
    long filesz;
    bool is_directory;
    
    res = _ftp->read_directory(filename, filesz, is_directory);
    bool exists = (res == false && !filename.empty());

    // The listing is streamed off the data socket now, so a caller that stops
    // early must close it or the control channel is left mid-transfer.
    _ftp->close_directory();
    
    Debug_printf("Path %s\n", exists ? "exists" : "does not exist");
    return exists;
}

bool FileSystemFTP::remove(const char *path)
{
    if (!_started || path == nullptr)
        return false;

    _ftp->close_directory();

    Debug_printf("FileSystemFTP::remove(\"%s\")\n", path);

    // Attempt to delete the file
    // delete_file returns FALSE on success, TRUE on error
    if (!_ftp->delete_file(path))
    {
        Debug_printf("File deleted successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to delete file\n");
        return false;
    }
}

bool FileSystemFTP::rename(const char *pathFrom, const char *pathTo)
{
    if (!_started || pathFrom == nullptr || pathTo == nullptr)
        return false;

    Debug_printf("FileSystemFTP::rename(\"%s\" -> \"%s\")\n", pathFrom, pathTo);

    // Attempt to rename the file
    // rename_file returns FALSE on success, TRUE on error
    if (!_ftp->rename_file(pathFrom, pathTo))
    {
        Debug_printf("File renamed successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to rename file\n");
        return false;
    }
}

FILE  *FileSystemFTP::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemFTP::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemFTP::filehandler_open(const char *path, const char *mode)
{
    FileHandler *fh = cache_file(path, mode);
    return fh;
}

// Read file from FTP path and write it to cache file
// Return FileHandler* on success (memory or SD file), nullptr on error
FileHandler *FileSystemFTP::cache_file(const char *path, const char *mode)
{
    Debug_printf("FileSystemFTP::cache_file(\"%s\", \"%s\")\n", path, mode);

    // Try SD cache first
    FileHandler *fh = FileCache::open(_url->mRawUrl.c_str(), path, mode);
    if (fh != nullptr)
        return fh; // cache hit, done

    // Create new cache file (starts in memory)
    fc_handle *fc = FileCache::create(_url->mRawUrl.c_str(), path);
    if (fc == nullptr)
        return nullptr;

    // Get file size from FTP server
    int32_t filesize = _ftp->get_file_size(path);
    Debug_printf("File size reported by server: %lu bytes\n", filesize);

    // Open FTP file
    Debug_println("Initiating file RETR");
    if (_ftp->open_file(path, false))
    {
        Debug_println("FileSystemFTP::cache_file - RETR failed");
        return nullptr;
    }

    // Retrieve FTP data
    bool cancel = false;
    int available;

    // Allocate copy buffer
    // uint8_t *buf = (uint8_t *)heap_caps_malloc(COPY_BLK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buf = (uint8_t *)malloc(COPY_BLK_SIZE);
    if (buf == nullptr)
    {
        Debug_println("FileSystemFTP::cache_file - failed to allocate buffer");
        return nullptr;
    }

    Debug_println("Retrieving file data");
    while ( !cancel )
    {
        available = _ftp->data_available();
        if (!_ftp->data_connected()) // done
            break;

        if (available == 0)
        {
            // No bytes in TCP receive buffer right now, but the data connection
            // is still open — the server is still sending.  Busy-wait on the
            // data_connected() check at the top of the loop rather than exiting
            // early.  Without this, large files that span multiple TCP segments
            // get truncated when the receive window empties between bursts.
            vTaskDelay(1);
            continue;
        }
        else if (available > 0)
        {
            while (available > 0)
            {
                if (!_ftp->data_connected()) // done
                    break;

                // Read FTP data
                int to_read = available > COPY_BLK_SIZE ? COPY_BLK_SIZE : available;
                if (_ftp->read_file(buf, to_read))
                {
                    //Debug_println("FileSystemFTP::cache_file - FTP read failed");
                    cancel = true;
                    break;
                }
                // Write cache file
                int written = FileCache::write(fc, buf, to_read);
                filesize -= written;
                if (written < to_read)
                {
                    //Debug_printf("FileSystemFTP::cache_file - Cache write failed\n");
                    cancel = true;
                    break;
                }
                // Next chunk
                available = _ftp->data_available();
            }

            if (filesize == 0)
                break;
        }
        else if (available < 0)
        {
            //Debug_println("FileSystemFTP::cache_file - something went wrong");
            cancel = true;
        }
    }
    // Release copy buffer
    free(buf);

    // Close FTP client
    _ftp->close();

    if (cancel)
    {
        Debug_println("Cancelled");
        FileCache::remove(fc);
        fh = nullptr;
    }
    else
    {
        Debug_println("File data retrieved");
        fh = FileCache::reopen(fc, mode);
    }
    return fh;
}
#endif //!FNIO_IS_STDIO

bool FileSystemFTP::is_dir(const char *path)
{
    if (!ensure_connected() || path == nullptr)
        return false;

    Debug_printf("FileSystemFTP::is_dir(\"%s\")\n", path);

    // CWD is the whole test: the server accepts it only for a directory, and
    // answers 550 for a file or a path that is not there - both of which are
    // "not a directory" here. This used to LIST the path and compare the first
    // entry's name against it, which downloaded the ENTIRE listing into
    // fnFTP::dirBuffer just to answer yes or no. On a board without PSRAM that
    // exhausted the internal heap and operator new aborted (ESP-IDF builds
    // -fno-exceptions). Every path fnFTP is given is absolute, so leaving the
    // server's working directory where CWD put it changes nothing.
    bool res = _ftp->change_directory(path);

    Debug_printf("Path is %s\n", res ? "a directory" : "not a directory");
    return res;
}

bool FileSystemFTP::mkdir(const char* path)
{
    if (!_started || path == nullptr)
        return false;

    _ftp->close_directory();

    Debug_printf("FileSystemFTP::mkdir(\"%s\")\n", path);

    // Attempt to create the directory
    // make_directory returns FALSE on success, TRUE on error
    if (!_ftp->make_directory(path))
    {
        Debug_printf("Directory created successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to create directory\n");
        return false;
    }
}

bool FileSystemFTP::rmdir(const char* path)
{
    if (!_started || path == nullptr)
        return false;

    _ftp->close_directory();

    Debug_printf("FileSystemFTP::rmdir(\"%s\")\n", path);

    // Attempt to remove the directory
    // remove_directory returns FALSE on success, TRUE on error
    if (!_ftp->remove_directory(path))
    {
        Debug_printf("Directory removed successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to remove directory\n");
        return false;
    }
}

bool FileSystemFTP::dir_exists(const char* path)
{
    // dir_exists is essentially the same as is_dir for FTP
    return is_dir(path);
}

#ifndef DISABLE_DIRCACHE

bool FileSystemFTP::dir_open(const char *path, const char *pattern, uint16_t diropts)
{
    if (!ensure_connected())
        return false;

    Debug_printf("FileSystemFTP::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);

    if (path == nullptr)
        return false;

    if (strcmp(_last_dir, path) == 0 && !_dircache.empty())
    {
        Debug_printf("Use directory cache\n");
    }
    else
    {
        Debug_printf("Fill directory cache\n");

        _dircache.clear();
        // invalidate _last_dir
        _last_dir[0] = '\0';

        // List FTP directory
        bool res;
        res = _ftp->open_directory(path, "");

        if (res)
        {
            Debug_printf("Failed to open directory\n");
            return false;
        }

        // Remember last visited directory
        strlcpy(_last_dir, path, MAX_PATHLEN);

        // Populate directory cache with entries
        string filename;
        long filesz;
        bool is_dir;
        fsdir_entry *fs_de;

        // get first directory entry
        res = _ftp->read_directory(filename, filesz, is_dir);
        while(res == false)
        {
            // skip hidden - fetch the next entry first, since `continue`
            // alone would re-test this same one forever.
            if (filename[0] == '.')
            {
                res = _ftp->read_directory(filename, filesz, is_dir);
                continue;
            }

            // new dir entry
            fs_de = &_dircache.new_entry();

            // set entry members
            strlcpy(fs_de->filename, filename.c_str(), sizeof(fs_de->filename));
            fs_de->isDir = is_dir;
            fs_de->size = (uint32_t)filesz;
            fs_de->modified_time = 0; // TODO

            // get next
            res = _ftp->read_directory(filename, filesz, is_dir);
        }
    }

    // Apply pattern matching filter and sort entries
    _dircache.apply_filter(pattern, diropts);

    return true;
}

fsdir_entry *FileSystemFTP::dir_read()
{
    return _dircache.read();
}

void FileSystemFTP::dir_close()
{
    // _dircache.clear();
}

uint16_t FileSystemFTP::dir_tell()
{
    return _dircache.tell();
}

bool FileSystemFTP::dir_seek(uint16_t pos)
{
    return _dircache.seek(pos);
}

#else // DISABLE_DIRCACHE

bool FileSystemFTP::dir_start(const char *path)
{
    _ftp->close_directory();
    _dirpos = 0;
    _dir_open = false;

    if (_ftp->open_directory(path, ""))
    {
        Debug_printf("Failed to open directory\n");
        return false;
    }

    _dir_open = true;
    return true;
}

bool FileSystemFTP::dir_open(const char *path, const char *pattern, uint16_t diropts)
{
    if (!ensure_connected())
        return false;

    Debug_printf("FileSystemFTP::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);

    if (path == nullptr)
        return false;

    // Sorting is not available without the cache: it would mean holding the
    // whole listing, which is exactly the cost this avoids. Entries come back
    // in server order. Nothing in Meatloaf asks for a sorted FTP listing -
    // FTPMFile passes DIR_OPTION_UNSORTED - so this only affects a fuji host slot.
    if (!(diropts & DIR_OPTION_UNSORTED))
        Debug_printf("FTP listings are unsorted (server order)\n");

    _dirpattern = (pattern != nullptr) ? pattern : "";

    if (!dir_start(path))
        return false;

    strlcpy(_last_dir, path, MAX_PATHLEN);
    return true;
}

fsdir_entry *FileSystemFTP::dir_read()
{
    if (!_dir_open)
        return nullptr;

    string filename;
    long filesz;
    bool is_dir;

    while (_ftp->read_directory(filename, filesz, is_dir) == false)
    {
        if (filename.empty() || filename[0] == '.')
            continue; // hidden

        // The filter is applied per entry as it arrives, since there is no
        // stored list to filter afterwards. Directories are matched only when
        // the pattern asks for them, as DirCache::apply_filter() does.
        if (!_dirpattern.empty())
        {
            bool filter_dirs = _dirpattern.back() == '/';
            if ((!is_dir || filter_dirs) &&
                util_wildcard_match(filename.c_str(), _dirpattern.c_str()) == false)
                continue;
        }

        strlcpy(_direntry.filename, filename.c_str(), sizeof(_direntry.filename));
        _direntry.isDir = is_dir;
        _direntry.size = (uint32_t)filesz;
        _direntry.modified_time = 0; // TODO
        _dirpos++;
        return &_direntry;
    }

    // End of listing - the data connection is already closed by read_directory().
    _dir_open = false;
    return nullptr;
}

void FileSystemFTP::dir_close()
{
    _ftp->close_directory();
    _dir_open = false;
    _dirpos = 0;
}

uint16_t FileSystemFTP::dir_tell()
{
    // Answers after the listing is exhausted too, as DirCache does - dir_seek()
    // can still get back there by re-listing.
    return (_last_dir[0] != '\0') ? _dirpos : FNFS_INVALID_DIRPOS;
}

bool FileSystemFTP::dir_seek(uint16_t pos)
{
    // Without a stored list the only way back is to LIST again and skip
    // forward. One network round trip per seek, which is why nothing should
    // seek in a loop - but it keeps fuji host-slot paging working.
    if (_last_dir[0] == '\0')
        return false;

    if (!dir_start(_last_dir))
        return false;

    while (_dirpos < pos)
    {
        if (dir_read() == nullptr)
            return false; // fewer entries than asked for
    }

    return true;
}

#endif // DISABLE_DIRCACHE

bool FileSystemFTP::keep_alive()
{
    if (!_started)
        return false;

    // Send NOOP command as lightweight keep-alive
    bool res = _ftp->keep_alive();
    
    if (!res) {
        Debug_printf("FTP keep_alive failed - marking session as disconnected\n");
        _started = false;
    }
    
    return res;
}

bool FileSystemFTP::ensure_connected()
{
    // A listing holds the data connection open until it is read to the end, so
    // close any that was abandoned before issuing something else. No-op when
    // none is open. dir_open() calls this before starting its own listing.
    if (_ftp != nullptr)
        _ftp->close_directory();

    // Check if we're actually connected at the FTP protocol level
    if (_started && _ftp && _ftp->control_connected()) {
        return true;  // Already connected and verified
    }
    
    // If we thought we were connected but aren't, mark as disconnected
    if (_started) {
        Debug_printf("FTP control connection lost, attempting reconnect\n");
        _started = false;
    }
    
    if (!_url || !_ftp) {
        Debug_printf("Cannot connect - missing URL or FTP client\n");
        return false;
    }
    
    if (_username.empty()) {
        Debug_printf("Cannot connect - credentials not set (start() was never called)\n");
        return false;
    }
    
    Debug_printf("Attempting to connect to FTP server: %s\n", _url->host.c_str());
    
    // Attempt to connect using stored credentials
    bool res = _ftp->login(
        _username.c_str(),
        _password.c_str(),
        _url->host,
        _url->port.empty() ? 21 : atoi(_url->port.c_str())
    );
    
    if (res) {
        Debug_printf("Failed to connect to FTP server\n");
        return false;
    }
    
    Debug_printf("Successfully connected to FTP server\n");
    _started = true;
    return true;
}