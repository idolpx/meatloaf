#ifndef FN_FILE_FTP_H
#define FN_FILE_FTP_H

#include <string>

#include "fnFile.h"
#include "fnFTP.h"

/*
 * A FileHandler that reads straight off a live FTP RETR data connection,
 * rather than downloading the file into a cache first.
 *
 * FileCache keeps a whole file in RAM until it passes its threshold and can
 * only spill to SD, so on a board without PSRAM - and without an SD card - a
 * file read needs as much internal heap as the file is long. Measured during a
 * transfer on an ESP32-WROOM32: 5960 bytes free, largest block 1972. A 7 KB
 * file could not be cached at all. This holds one small buffer instead, so the
 * cost is the same whatever the file's size.
 *
 * The transfer starts LAZILY, on the first read. That is what lets callers do
 * the usual seek-to-end/tell/seek-to-start size probe without any network
 * traffic - the size is known from SIZE up front.
 */
class FileHandlerFTP : public FileHandler
{
public:
    FileHandlerFTP(fnFTP *ftp, const std::string &path);
    virtual ~FileHandlerFTP() override;

    int close(bool destroy = true) override;
    int seek(long int off, int whence) override;
    long int tell() override;
    size_t read(void *ptr, size_t size, size_t n) override;
    size_t write(const void *ptr, size_t size, size_t n) override;
    int flush() override;
    int eof() override;

    // File size, from the server's SIZE response. Negative if unknown.
    long int size() const { return _size; }

private:
    bool start_transfer();      // begin RETR at _pos, if not already running
    void stop_transfer();       // end the current RETR, if any
    bool skip_forward(long int target);

    fnFTP *_ftp;
    std::string _path;
    long int _size;             // from SIZE, -1 if the server would not say
    long int _pos;              // where the NEXT read starts
    bool _started;              // a RETR is in progress at _pos
};

#endif // FN_FILE_FTP_H
