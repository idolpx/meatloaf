
#include "fnFileFTP.h"

#include <errno.h>
#include <string.h>

#include "../../include/debug.h"

#include "fnSystem.h"

// Bytes pulled from the data connection per read call, and the size of the
// throwaway buffer a forward seek discards through. Deliberately small: this
// runs on boards where the largest free block during a transfer is under 2 KB.
#define FTP_STREAM_CHUNK 512

// A backwards seek, or a forward seek past this many bytes, restarts the
// transfer with REST instead of reading and discarding. Restarting costs a
// control round trip plus a new data connection; discarding costs the transfer
// time for the bytes skipped. This is the break-even guess, not a measurement.
#define FTP_STREAM_SKIP_LIMIT 8192

FileHandlerFTP::FileHandlerFTP(fnFTP *ftp, const std::string &path)
    : _ftp(ftp), _path(path), _size(-1), _pos(0), _started(false)
{
    if (_ftp != nullptr)
    {
        int32_t sz = _ftp->get_file_size(_path);
        if (sz >= 0)
            _size = sz;
    }
    Debug_printf("FileHandlerFTP(\"%s\") size=%ld\n", _path.c_str(), _size);
}

FileHandlerFTP::~FileHandlerFTP()
{
    stop_transfer();
}

bool FileHandlerFTP::start_transfer()
{
    if (_started)
        return true;

    if (_ftp == nullptr)
    {
        errno = EBADF;
        return false;
    }

    if (_ftp->open_file(_path, false, (unsigned long)_pos))
    {
        Debug_printf("FileHandlerFTP - RETR failed at offset %ld\n", _pos);
        errno = EIO;
        return false;
    }

    _started = true;
    return true;
}

void FileHandlerFTP::stop_transfer()
{
    if (!_started)
        return;

    _started = false;
    if (_ftp != nullptr)
        _ftp->end_transfer();
}

// Read and discard up to target. Used for a short forward seek, where that is
// cheaper than a new data connection.
bool FileHandlerFTP::skip_forward(long int target)
{
    uint8_t buf[FTP_STREAM_CHUNK];

    while (_pos < target)
    {
        size_t want = (size_t)(target - _pos);
        if (want > sizeof(buf))
            want = sizeof(buf);

        size_t got = read(buf, 1, want);
        if (got == 0)
            return false;
    }

    return true;
}

size_t FileHandlerFTP::read(void *ptr, size_t size, size_t n)
{
    if (ptr == nullptr || size == 0 || n == 0)
        return 0;

    if (!start_transfer())
        return 0;

    size_t want = size * n;
    uint8_t *out = (uint8_t *)ptr;
    size_t total = 0;

    while (total < want)
    {
        // The server closes the data connection at end of file, so an empty
        // socket is not necessarily the end - wait while it is still open.
        int avail = _ftp->data_available();
        if (avail <= 0)
        {
            if (!_ftp->data_connected())
                break; // end of file
            fnSystem.delay(1);
            continue;
        }

        size_t chunk = want - total;
        if (chunk > (size_t)avail)
            chunk = (size_t)avail;
        if (chunk > FTP_STREAM_CHUNK)
            chunk = FTP_STREAM_CHUNK;

        if (_ftp->read_file(out + total, (unsigned short)chunk))
            break; // read error or socket gone

        total += chunk;
        _pos += chunk;

        if (_size >= 0 && _pos >= _size)
            break; // whole file delivered
    }

    // Whole-elements semantics, as fread has.
    return (size > 1) ? (total / size) : total;
}

size_t FileHandlerFTP::write(const void *ptr, size_t size, size_t n)
{
    // Read-only: this handle exists for RETR. STOR goes through its own path.
    errno = EBADF;
    return 0;
}

int FileHandlerFTP::seek(long int off, int whence)
{
    long int target;

    switch (whence)
    {
    case SEEK_SET:
        target = off;
        break;
    case SEEK_CUR:
        target = _pos + off;
        break;
    case SEEK_END:
        if (_size < 0)
        {
            errno = ESPIPE; // no size from the server, so no end to seek to
            return -1;
        }
        target = _size + off;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    if (target < 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (target == _pos)
        return 0;

    // Nothing is in flight yet, so moving is free - this is the path the
    // seek-to-end/tell/seek-to-start size probe takes at open time.
    if (!_started)
    {
        _pos = target;
        return 0;
    }

    if (target > _pos && target - _pos <= FTP_STREAM_SKIP_LIMIT)
    {
        if (skip_forward(target))
            return 0;
        // Fall through and restart if discarding failed.
    }

    stop_transfer();
    _pos = target;
    return 0; // the next read starts a transfer at _pos, via REST
}

long int FileHandlerFTP::tell()
{
    return _pos;
}

int FileHandlerFTP::eof()
{
    if (_size >= 0)
        return _pos >= _size ? 1 : 0;

    // Size unknown: only the data connection can say.
    return (_started && !_ftp->data_connected() && _ftp->data_available() == 0) ? 1 : 0;
}

int FileHandlerFTP::flush()
{
    return 0;
}

int FileHandlerFTP::close(bool destroy)
{
    stop_transfer();

    if (destroy)
        delete this;

    return 0;
}
